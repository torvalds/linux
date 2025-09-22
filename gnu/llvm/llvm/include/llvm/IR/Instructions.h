//===- llvm/Instructions.h - Instruction subclass definitions ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes the class definitions of all of the subclasses of the
// Instruction class.  This is meant to be an easy way to get access to all
// instruction subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INSTRUCTIONS_H
#define LLVM_IR_INSTRUCTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Bitfields.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GEPNoWrapFlags.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>

namespace llvm {

class APFloat;
class APInt;
class BasicBlock;
class ConstantInt;
class DataLayout;
class StringRef;
class Type;
class Value;
class UnreachableInst;

//===----------------------------------------------------------------------===//
//                                AllocaInst Class
//===----------------------------------------------------------------------===//

/// an instruction to allocate memory on the stack
class AllocaInst : public UnaryInstruction {
  Type *AllocatedType;

  using AlignmentField = AlignmentBitfieldElementT<0>;
  using UsedWithInAllocaField = BoolBitfieldElementT<AlignmentField::NextBit>;
  using SwiftErrorField = BoolBitfieldElementT<UsedWithInAllocaField::NextBit>;
  static_assert(Bitfield::areContiguous<AlignmentField, UsedWithInAllocaField,
                                        SwiftErrorField>(),
                "Bitfields must be contiguous");

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  AllocaInst *cloneImpl() const;

public:
  explicit AllocaInst(Type *Ty, unsigned AddrSpace, Value *ArraySize,
                      const Twine &Name, InsertPosition InsertBefore);

  AllocaInst(Type *Ty, unsigned AddrSpace, const Twine &Name,
             InsertPosition InsertBefore);

  AllocaInst(Type *Ty, unsigned AddrSpace, Value *ArraySize, Align Align,
             const Twine &Name = "", InsertPosition InsertBefore = nullptr);

  /// Return true if there is an allocation size parameter to the allocation
  /// instruction that is not 1.
  bool isArrayAllocation() const;

  /// Get the number of elements allocated. For a simple allocation of a single
  /// element, this will return a constant 1 value.
  const Value *getArraySize() const { return getOperand(0); }
  Value *getArraySize() { return getOperand(0); }

  /// Overload to return most specific pointer type.
  PointerType *getType() const {
    return cast<PointerType>(Instruction::getType());
  }

  /// Return the address space for the allocation.
  unsigned getAddressSpace() const {
    return getType()->getAddressSpace();
  }

  /// Get allocation size in bytes. Returns std::nullopt if size can't be
  /// determined, e.g. in case of a VLA.
  std::optional<TypeSize> getAllocationSize(const DataLayout &DL) const;

  /// Get allocation size in bits. Returns std::nullopt if size can't be
  /// determined, e.g. in case of a VLA.
  std::optional<TypeSize> getAllocationSizeInBits(const DataLayout &DL) const;

  /// Return the type that is being allocated by the instruction.
  Type *getAllocatedType() const { return AllocatedType; }
  /// for use only in special circumstances that need to generically
  /// transform a whole instruction (eg: IR linking and vectorization).
  void setAllocatedType(Type *Ty) { AllocatedType = Ty; }

  /// Return the alignment of the memory that is being allocated by the
  /// instruction.
  Align getAlign() const {
    return Align(1ULL << getSubclassData<AlignmentField>());
  }

  void setAlignment(Align Align) {
    setSubclassData<AlignmentField>(Log2(Align));
  }

  /// Return true if this alloca is in the entry block of the function and is a
  /// constant size. If so, the code generator will fold it into the
  /// prolog/epilog code, so it is basically free.
  bool isStaticAlloca() const;

  /// Return true if this alloca is used as an inalloca argument to a call. Such
  /// allocas are never considered static even if they are in the entry block.
  bool isUsedWithInAlloca() const {
    return getSubclassData<UsedWithInAllocaField>();
  }

  /// Specify whether this alloca is used to represent the arguments to a call.
  void setUsedWithInAlloca(bool V) {
    setSubclassData<UsedWithInAllocaField>(V);
  }

  /// Return true if this alloca is used as a swifterror argument to a call.
  bool isSwiftError() const { return getSubclassData<SwiftErrorField>(); }
  /// Specify whether this alloca is used to represent a swifterror.
  void setSwiftError(bool V) { setSubclassData<SwiftErrorField>(V); }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::Alloca);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  template <typename Bitfield>
  void setSubclassData(typename Bitfield::Type Value) {
    Instruction::setSubclassData<Bitfield>(Value);
  }
};

//===----------------------------------------------------------------------===//
//                                LoadInst Class
//===----------------------------------------------------------------------===//

/// An instruction for reading from memory. This uses the SubclassData field in
/// Value to store whether or not the load is volatile.
class LoadInst : public UnaryInstruction {
  using VolatileField = BoolBitfieldElementT<0>;
  using AlignmentField = AlignmentBitfieldElementT<VolatileField::NextBit>;
  using OrderingField = AtomicOrderingBitfieldElementT<AlignmentField::NextBit>;
  static_assert(
      Bitfield::areContiguous<VolatileField, AlignmentField, OrderingField>(),
      "Bitfields must be contiguous");

  void AssertOK();

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  LoadInst *cloneImpl() const;

public:
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr,
           InsertPosition InsertBefore);
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr, bool isVolatile,
           InsertPosition InsertBefore);
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr, bool isVolatile,
           Align Align, InsertPosition InsertBefore = nullptr);
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr, bool isVolatile,
           Align Align, AtomicOrdering Order,
           SyncScope::ID SSID = SyncScope::System,
           InsertPosition InsertBefore = nullptr);

  /// Return true if this is a load from a volatile memory location.
  bool isVolatile() const { return getSubclassData<VolatileField>(); }

  /// Specify whether this is a volatile load or not.
  void setVolatile(bool V) { setSubclassData<VolatileField>(V); }

  /// Return the alignment of the access that is being performed.
  Align getAlign() const {
    return Align(1ULL << (getSubclassData<AlignmentField>()));
  }

  void setAlignment(Align Align) {
    setSubclassData<AlignmentField>(Log2(Align));
  }

  /// Returns the ordering constraint of this load instruction.
  AtomicOrdering getOrdering() const {
    return getSubclassData<OrderingField>();
  }
  /// Sets the ordering constraint of this load instruction.  May not be Release
  /// or AcquireRelease.
  void setOrdering(AtomicOrdering Ordering) {
    setSubclassData<OrderingField>(Ordering);
  }

  /// Returns the synchronization scope ID of this load instruction.
  SyncScope::ID getSyncScopeID() const {
    return SSID;
  }

  /// Sets the synchronization scope ID of this load instruction.
  void setSyncScopeID(SyncScope::ID SSID) {
    this->SSID = SSID;
  }

  /// Sets the ordering constraint and the synchronization scope ID of this load
  /// instruction.
  void setAtomic(AtomicOrdering Ordering,
                 SyncScope::ID SSID = SyncScope::System) {
    setOrdering(Ordering);
    setSyncScopeID(SSID);
  }

  bool isSimple() const { return !isAtomic() && !isVolatile(); }

  bool isUnordered() const {
    return (getOrdering() == AtomicOrdering::NotAtomic ||
            getOrdering() == AtomicOrdering::Unordered) &&
           !isVolatile();
  }

  Value *getPointerOperand() { return getOperand(0); }
  const Value *getPointerOperand() const { return getOperand(0); }
  static unsigned getPointerOperandIndex() { return 0U; }
  Type *getPointerOperandType() const { return getPointerOperand()->getType(); }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperandType()->getPointerAddressSpace();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Load;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  template <typename Bitfield>
  void setSubclassData(typename Bitfield::Type Value) {
    Instruction::setSubclassData<Bitfield>(Value);
  }

  /// The synchronization scope ID of this load instruction.  Not quite enough
  /// room in SubClassData for everything, so synchronization scope ID gets its
  /// own field.
  SyncScope::ID SSID;
};

//===----------------------------------------------------------------------===//
//                                StoreInst Class
//===----------------------------------------------------------------------===//

/// An instruction for storing to memory.
class StoreInst : public Instruction {
  using VolatileField = BoolBitfieldElementT<0>;
  using AlignmentField = AlignmentBitfieldElementT<VolatileField::NextBit>;
  using OrderingField = AtomicOrderingBitfieldElementT<AlignmentField::NextBit>;
  static_assert(
      Bitfield::areContiguous<VolatileField, AlignmentField, OrderingField>(),
      "Bitfields must be contiguous");

  void AssertOK();

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  StoreInst *cloneImpl() const;

public:
  StoreInst(Value *Val, Value *Ptr, InsertPosition InsertBefore);
  StoreInst(Value *Val, Value *Ptr, bool isVolatile,
            InsertPosition InsertBefore);
  StoreInst(Value *Val, Value *Ptr, bool isVolatile, Align Align,
            InsertPosition InsertBefore = nullptr);
  StoreInst(Value *Val, Value *Ptr, bool isVolatile, Align Align,
            AtomicOrdering Order, SyncScope::ID SSID = SyncScope::System,
            InsertPosition InsertBefore = nullptr);

  // allocate space for exactly two operands
  void *operator new(size_t S) { return User::operator new(S, 2); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  /// Return true if this is a store to a volatile memory location.
  bool isVolatile() const { return getSubclassData<VolatileField>(); }

  /// Specify whether this is a volatile store or not.
  void setVolatile(bool V) { setSubclassData<VolatileField>(V); }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  Align getAlign() const {
    return Align(1ULL << (getSubclassData<AlignmentField>()));
  }

  void setAlignment(Align Align) {
    setSubclassData<AlignmentField>(Log2(Align));
  }

  /// Returns the ordering constraint of this store instruction.
  AtomicOrdering getOrdering() const {
    return getSubclassData<OrderingField>();
  }

  /// Sets the ordering constraint of this store instruction.  May not be
  /// Acquire or AcquireRelease.
  void setOrdering(AtomicOrdering Ordering) {
    setSubclassData<OrderingField>(Ordering);
  }

  /// Returns the synchronization scope ID of this store instruction.
  SyncScope::ID getSyncScopeID() const {
    return SSID;
  }

  /// Sets the synchronization scope ID of this store instruction.
  void setSyncScopeID(SyncScope::ID SSID) {
    this->SSID = SSID;
  }

  /// Sets the ordering constraint and the synchronization scope ID of this
  /// store instruction.
  void setAtomic(AtomicOrdering Ordering,
                 SyncScope::ID SSID = SyncScope::System) {
    setOrdering(Ordering);
    setSyncScopeID(SSID);
  }

  bool isSimple() const { return !isAtomic() && !isVolatile(); }

  bool isUnordered() const {
    return (getOrdering() == AtomicOrdering::NotAtomic ||
            getOrdering() == AtomicOrdering::Unordered) &&
           !isVolatile();
  }

  Value *getValueOperand() { return getOperand(0); }
  const Value *getValueOperand() const { return getOperand(0); }

  Value *getPointerOperand() { return getOperand(1); }
  const Value *getPointerOperand() const { return getOperand(1); }
  static unsigned getPointerOperandIndex() { return 1U; }
  Type *getPointerOperandType() const { return getPointerOperand()->getType(); }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperandType()->getPointerAddressSpace();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Store;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  template <typename Bitfield>
  void setSubclassData(typename Bitfield::Type Value) {
    Instruction::setSubclassData<Bitfield>(Value);
  }

  /// The synchronization scope ID of this store instruction.  Not quite enough
  /// room in SubClassData for everything, so synchronization scope ID gets its
  /// own field.
  SyncScope::ID SSID;
};

template <>
struct OperandTraits<StoreInst> : public FixedNumOperandTraits<StoreInst, 2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(StoreInst, Value)

//===----------------------------------------------------------------------===//
//                                FenceInst Class
//===----------------------------------------------------------------------===//

/// An instruction for ordering other memory operations.
class FenceInst : public Instruction {
  using OrderingField = AtomicOrderingBitfieldElementT<0>;

  void Init(AtomicOrdering Ordering, SyncScope::ID SSID);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  FenceInst *cloneImpl() const;

public:
  // Ordering may only be Acquire, Release, AcquireRelease, or
  // SequentiallyConsistent.
  FenceInst(LLVMContext &C, AtomicOrdering Ordering,
            SyncScope::ID SSID = SyncScope::System,
            InsertPosition InsertBefore = nullptr);

  // allocate space for exactly zero operands
  void *operator new(size_t S) { return User::operator new(S, 0); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  /// Returns the ordering constraint of this fence instruction.
  AtomicOrdering getOrdering() const {
    return getSubclassData<OrderingField>();
  }

  /// Sets the ordering constraint of this fence instruction.  May only be
  /// Acquire, Release, AcquireRelease, or SequentiallyConsistent.
  void setOrdering(AtomicOrdering Ordering) {
    setSubclassData<OrderingField>(Ordering);
  }

  /// Returns the synchronization scope ID of this fence instruction.
  SyncScope::ID getSyncScopeID() const {
    return SSID;
  }

  /// Sets the synchronization scope ID of this fence instruction.
  void setSyncScopeID(SyncScope::ID SSID) {
    this->SSID = SSID;
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Fence;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  template <typename Bitfield>
  void setSubclassData(typename Bitfield::Type Value) {
    Instruction::setSubclassData<Bitfield>(Value);
  }

  /// The synchronization scope ID of this fence instruction.  Not quite enough
  /// room in SubClassData for everything, so synchronization scope ID gets its
  /// own field.
  SyncScope::ID SSID;
};

//===----------------------------------------------------------------------===//
//                                AtomicCmpXchgInst Class
//===----------------------------------------------------------------------===//

/// An instruction that atomically checks whether a
/// specified value is in a memory location, and, if it is, stores a new value
/// there. The value returned by this instruction is a pair containing the
/// original value as first element, and an i1 indicating success (true) or
/// failure (false) as second element.
///
class AtomicCmpXchgInst : public Instruction {
  void Init(Value *Ptr, Value *Cmp, Value *NewVal, Align Align,
            AtomicOrdering SuccessOrdering, AtomicOrdering FailureOrdering,
            SyncScope::ID SSID);

  template <unsigned Offset>
  using AtomicOrderingBitfieldElement =
      typename Bitfield::Element<AtomicOrdering, Offset, 3,
                                 AtomicOrdering::LAST>;

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  AtomicCmpXchgInst *cloneImpl() const;

public:
  AtomicCmpXchgInst(Value *Ptr, Value *Cmp, Value *NewVal, Align Alignment,
                    AtomicOrdering SuccessOrdering,
                    AtomicOrdering FailureOrdering, SyncScope::ID SSID,
                    InsertPosition InsertBefore = nullptr);

  // allocate space for exactly three operands
  void *operator new(size_t S) { return User::operator new(S, 3); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  using VolatileField = BoolBitfieldElementT<0>;
  using WeakField = BoolBitfieldElementT<VolatileField::NextBit>;
  using SuccessOrderingField =
      AtomicOrderingBitfieldElementT<WeakField::NextBit>;
  using FailureOrderingField =
      AtomicOrderingBitfieldElementT<SuccessOrderingField::NextBit>;
  using AlignmentField =
      AlignmentBitfieldElementT<FailureOrderingField::NextBit>;
  static_assert(
      Bitfield::areContiguous<VolatileField, WeakField, SuccessOrderingField,
                              FailureOrderingField, AlignmentField>(),
      "Bitfields must be contiguous");

  /// Return the alignment of the memory that is being allocated by the
  /// instruction.
  Align getAlign() const {
    return Align(1ULL << getSubclassData<AlignmentField>());
  }

  void setAlignment(Align Align) {
    setSubclassData<AlignmentField>(Log2(Align));
  }

  /// Return true if this is a cmpxchg from a volatile memory
  /// location.
  ///
  bool isVolatile() const { return getSubclassData<VolatileField>(); }

  /// Specify whether this is a volatile cmpxchg.
  ///
  void setVolatile(bool V) { setSubclassData<VolatileField>(V); }

  /// Return true if this cmpxchg may spuriously fail.
  bool isWeak() const { return getSubclassData<WeakField>(); }

  void setWeak(bool IsWeak) { setSubclassData<WeakField>(IsWeak); }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  static bool isValidSuccessOrdering(AtomicOrdering Ordering) {
    return Ordering != AtomicOrdering::NotAtomic &&
           Ordering != AtomicOrdering::Unordered;
  }

  static bool isValidFailureOrdering(AtomicOrdering Ordering) {
    return Ordering != AtomicOrdering::NotAtomic &&
           Ordering != AtomicOrdering::Unordered &&
           Ordering != AtomicOrdering::AcquireRelease &&
           Ordering != AtomicOrdering::Release;
  }

  /// Returns the success ordering constraint of this cmpxchg instruction.
  AtomicOrdering getSuccessOrdering() const {
    return getSubclassData<SuccessOrderingField>();
  }

  /// Sets the success ordering constraint of this cmpxchg instruction.
  void setSuccessOrdering(AtomicOrdering Ordering) {
    assert(isValidSuccessOrdering(Ordering) &&
           "invalid CmpXchg success ordering");
    setSubclassData<SuccessOrderingField>(Ordering);
  }

  /// Returns the failure ordering constraint of this cmpxchg instruction.
  AtomicOrdering getFailureOrdering() const {
    return getSubclassData<FailureOrderingField>();
  }

  /// Sets the failure ordering constraint of this cmpxchg instruction.
  void setFailureOrdering(AtomicOrdering Ordering) {
    assert(isValidFailureOrdering(Ordering) &&
           "invalid CmpXchg failure ordering");
    setSubclassData<FailureOrderingField>(Ordering);
  }

  /// Returns a single ordering which is at least as strong as both the
  /// success and failure orderings for this cmpxchg.
  AtomicOrdering getMergedOrdering() const {
    if (getFailureOrdering() == AtomicOrdering::SequentiallyConsistent)
      return AtomicOrdering::SequentiallyConsistent;
    if (getFailureOrdering() == AtomicOrdering::Acquire) {
      if (getSuccessOrdering() == AtomicOrdering::Monotonic)
        return AtomicOrdering::Acquire;
      if (getSuccessOrdering() == AtomicOrdering::Release)
        return AtomicOrdering::AcquireRelease;
    }
    return getSuccessOrdering();
  }

  /// Returns the synchronization scope ID of this cmpxchg instruction.
  SyncScope::ID getSyncScopeID() const {
    return SSID;
  }

  /// Sets the synchronization scope ID of this cmpxchg instruction.
  void setSyncScopeID(SyncScope::ID SSID) {
    this->SSID = SSID;
  }

  Value *getPointerOperand() { return getOperand(0); }
  const Value *getPointerOperand() const { return getOperand(0); }
  static unsigned getPointerOperandIndex() { return 0U; }

  Value *getCompareOperand() { return getOperand(1); }
  const Value *getCompareOperand() const { return getOperand(1); }

  Value *getNewValOperand() { return getOperand(2); }
  const Value *getNewValOperand() const { return getOperand(2); }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  /// Returns the strongest permitted ordering on failure, given the
  /// desired ordering on success.
  ///
  /// If the comparison in a cmpxchg operation fails, there is no atomic store
  /// so release semantics cannot be provided. So this function drops explicit
  /// Release requests from the AtomicOrdering. A SequentiallyConsistent
  /// operation would remain SequentiallyConsistent.
  static AtomicOrdering
  getStrongestFailureOrdering(AtomicOrdering SuccessOrdering) {
    switch (SuccessOrdering) {
    default:
      llvm_unreachable("invalid cmpxchg success ordering");
    case AtomicOrdering::Release:
    case AtomicOrdering::Monotonic:
      return AtomicOrdering::Monotonic;
    case AtomicOrdering::AcquireRelease:
    case AtomicOrdering::Acquire:
      return AtomicOrdering::Acquire;
    case AtomicOrdering::SequentiallyConsistent:
      return AtomicOrdering::SequentiallyConsistent;
    }
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::AtomicCmpXchg;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  template <typename Bitfield>
  void setSubclassData(typename Bitfield::Type Value) {
    Instruction::setSubclassData<Bitfield>(Value);
  }

  /// The synchronization scope ID of this cmpxchg instruction.  Not quite
  /// enough room in SubClassData for everything, so synchronization scope ID
  /// gets its own field.
  SyncScope::ID SSID;
};

template <>
struct OperandTraits<AtomicCmpXchgInst> :
    public FixedNumOperandTraits<AtomicCmpXchgInst, 3> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(AtomicCmpXchgInst, Value)

//===----------------------------------------------------------------------===//
//                                AtomicRMWInst Class
//===----------------------------------------------------------------------===//

/// an instruction that atomically reads a memory location,
/// combines it with another value, and then stores the result back.  Returns
/// the old value.
///
class AtomicRMWInst : public Instruction {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  AtomicRMWInst *cloneImpl() const;

public:
  /// This enumeration lists the possible modifications atomicrmw can make.  In
  /// the descriptions, 'p' is the pointer to the instruction's memory location,
  /// 'old' is the initial value of *p, and 'v' is the other value passed to the
  /// instruction.  These instructions always return 'old'.
  enum BinOp : unsigned {
    /// *p = v
    Xchg,
    /// *p = old + v
    Add,
    /// *p = old - v
    Sub,
    /// *p = old & v
    And,
    /// *p = ~(old & v)
    Nand,
    /// *p = old | v
    Or,
    /// *p = old ^ v
    Xor,
    /// *p = old >signed v ? old : v
    Max,
    /// *p = old <signed v ? old : v
    Min,
    /// *p = old >unsigned v ? old : v
    UMax,
    /// *p = old <unsigned v ? old : v
    UMin,

    /// *p = old + v
    FAdd,

    /// *p = old - v
    FSub,

    /// *p = maxnum(old, v)
    /// \p maxnum matches the behavior of \p llvm.maxnum.*.
    FMax,

    /// *p = minnum(old, v)
    /// \p minnum matches the behavior of \p llvm.minnum.*.
    FMin,

    /// Increment one up to a maximum value.
    /// *p = (old u>= v) ? 0 : (old + 1)
    UIncWrap,

    /// Decrement one until a minimum value or zero.
    /// *p = ((old == 0) || (old u> v)) ? v : (old - 1)
    UDecWrap,

    FIRST_BINOP = Xchg,
    LAST_BINOP = UDecWrap,
    BAD_BINOP
  };

private:
  template <unsigned Offset>
  using AtomicOrderingBitfieldElement =
      typename Bitfield::Element<AtomicOrdering, Offset, 3,
                                 AtomicOrdering::LAST>;

  template <unsigned Offset>
  using BinOpBitfieldElement =
      typename Bitfield::Element<BinOp, Offset, 5, BinOp::LAST_BINOP>;

public:
  AtomicRMWInst(BinOp Operation, Value *Ptr, Value *Val, Align Alignment,
                AtomicOrdering Ordering, SyncScope::ID SSID,
                InsertPosition InsertBefore = nullptr);

  // allocate space for exactly two operands
  void *operator new(size_t S) { return User::operator new(S, 2); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  using VolatileField = BoolBitfieldElementT<0>;
  using AtomicOrderingField =
      AtomicOrderingBitfieldElementT<VolatileField::NextBit>;
  using OperationField = BinOpBitfieldElement<AtomicOrderingField::NextBit>;
  using AlignmentField = AlignmentBitfieldElementT<OperationField::NextBit>;
  static_assert(Bitfield::areContiguous<VolatileField, AtomicOrderingField,
                                        OperationField, AlignmentField>(),
                "Bitfields must be contiguous");

  BinOp getOperation() const { return getSubclassData<OperationField>(); }

  static StringRef getOperationName(BinOp Op);

  static bool isFPOperation(BinOp Op) {
    switch (Op) {
    case AtomicRMWInst::FAdd:
    case AtomicRMWInst::FSub:
    case AtomicRMWInst::FMax:
    case AtomicRMWInst::FMin:
      return true;
    default:
      return false;
    }
  }

  void setOperation(BinOp Operation) {
    setSubclassData<OperationField>(Operation);
  }

  /// Return the alignment of the memory that is being allocated by the
  /// instruction.
  Align getAlign() const {
    return Align(1ULL << getSubclassData<AlignmentField>());
  }

  void setAlignment(Align Align) {
    setSubclassData<AlignmentField>(Log2(Align));
  }

  /// Return true if this is a RMW on a volatile memory location.
  ///
  bool isVolatile() const { return getSubclassData<VolatileField>(); }

  /// Specify whether this is a volatile RMW or not.
  ///
  void setVolatile(bool V) { setSubclassData<VolatileField>(V); }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Returns the ordering constraint of this rmw instruction.
  AtomicOrdering getOrdering() const {
    return getSubclassData<AtomicOrderingField>();
  }

  /// Sets the ordering constraint of this rmw instruction.
  void setOrdering(AtomicOrdering Ordering) {
    assert(Ordering != AtomicOrdering::NotAtomic &&
           "atomicrmw instructions can only be atomic.");
    assert(Ordering != AtomicOrdering::Unordered &&
           "atomicrmw instructions cannot be unordered.");
    setSubclassData<AtomicOrderingField>(Ordering);
  }

  /// Returns the synchronization scope ID of this rmw instruction.
  SyncScope::ID getSyncScopeID() const {
    return SSID;
  }

  /// Sets the synchronization scope ID of this rmw instruction.
  void setSyncScopeID(SyncScope::ID SSID) {
    this->SSID = SSID;
  }

  Value *getPointerOperand() { return getOperand(0); }
  const Value *getPointerOperand() const { return getOperand(0); }
  static unsigned getPointerOperandIndex() { return 0U; }

  Value *getValOperand() { return getOperand(1); }
  const Value *getValOperand() const { return getOperand(1); }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  bool isFloatingPointOperation() const {
    return isFPOperation(getOperation());
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::AtomicRMW;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  void Init(BinOp Operation, Value *Ptr, Value *Val, Align Align,
            AtomicOrdering Ordering, SyncScope::ID SSID);

  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  template <typename Bitfield>
  void setSubclassData(typename Bitfield::Type Value) {
    Instruction::setSubclassData<Bitfield>(Value);
  }

  /// The synchronization scope ID of this rmw instruction.  Not quite enough
  /// room in SubClassData for everything, so synchronization scope ID gets its
  /// own field.
  SyncScope::ID SSID;
};

template <>
struct OperandTraits<AtomicRMWInst>
    : public FixedNumOperandTraits<AtomicRMWInst,2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(AtomicRMWInst, Value)

//===----------------------------------------------------------------------===//
//                             GetElementPtrInst Class
//===----------------------------------------------------------------------===//

// checkGEPType - Simple wrapper function to give a better assertion failure
// message on bad indexes for a gep instruction.
//
inline Type *checkGEPType(Type *Ty) {
  assert(Ty && "Invalid GetElementPtrInst indices for type!");
  return Ty;
}

/// an instruction for type-safe pointer arithmetic to
/// access elements of arrays and structs
///
class GetElementPtrInst : public Instruction {
  Type *SourceElementType;
  Type *ResultElementType;

  GetElementPtrInst(const GetElementPtrInst &GEPI);

  /// Constructors - Create a getelementptr instruction with a base pointer an
  /// list of indices. The first and second ctor can optionally insert before an
  /// existing instruction, the third appends the new instruction to the
  /// specified BasicBlock.
  inline GetElementPtrInst(Type *PointeeType, Value *Ptr,
                           ArrayRef<Value *> IdxList, unsigned Values,
                           const Twine &NameStr, InsertPosition InsertBefore);

  void init(Value *Ptr, ArrayRef<Value *> IdxList, const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  GetElementPtrInst *cloneImpl() const;

public:
  static GetElementPtrInst *Create(Type *PointeeType, Value *Ptr,
                                   ArrayRef<Value *> IdxList,
                                   const Twine &NameStr = "",
                                   InsertPosition InsertBefore = nullptr) {
    unsigned Values = 1 + unsigned(IdxList.size());
    assert(PointeeType && "Must specify element type");
    return new (Values) GetElementPtrInst(PointeeType, Ptr, IdxList, Values,
                                          NameStr, InsertBefore);
  }

  static GetElementPtrInst *Create(Type *PointeeType, Value *Ptr,
                                   ArrayRef<Value *> IdxList, GEPNoWrapFlags NW,
                                   const Twine &NameStr = "",
                                   InsertPosition InsertBefore = nullptr) {
    GetElementPtrInst *GEP =
        Create(PointeeType, Ptr, IdxList, NameStr, InsertBefore);
    GEP->setNoWrapFlags(NW);
    return GEP;
  }

  /// Create an "inbounds" getelementptr. See the documentation for the
  /// "inbounds" flag in LangRef.html for details.
  static GetElementPtrInst *
  CreateInBounds(Type *PointeeType, Value *Ptr, ArrayRef<Value *> IdxList,
                 const Twine &NameStr = "",
                 InsertPosition InsertBefore = nullptr) {
    return Create(PointeeType, Ptr, IdxList, GEPNoWrapFlags::inBounds(),
                  NameStr, InsertBefore);
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  Type *getSourceElementType() const { return SourceElementType; }

  void setSourceElementType(Type *Ty) { SourceElementType = Ty; }
  void setResultElementType(Type *Ty) { ResultElementType = Ty; }

  Type *getResultElementType() const {
    return ResultElementType;
  }

  /// Returns the address space of this instruction's pointer type.
  unsigned getAddressSpace() const {
    // Note that this is always the same as the pointer operand's address space
    // and that is cheaper to compute, so cheat here.
    return getPointerAddressSpace();
  }

  /// Returns the result type of a getelementptr with the given source
  /// element type and indexes.
  ///
  /// Null is returned if the indices are invalid for the specified
  /// source element type.
  static Type *getIndexedType(Type *Ty, ArrayRef<Value *> IdxList);
  static Type *getIndexedType(Type *Ty, ArrayRef<Constant *> IdxList);
  static Type *getIndexedType(Type *Ty, ArrayRef<uint64_t> IdxList);

  /// Return the type of the element at the given index of an indexable
  /// type.  This is equivalent to "getIndexedType(Agg, {Zero, Idx})".
  ///
  /// Returns null if the type can't be indexed, or the given index is not
  /// legal for the given type.
  static Type *getTypeAtIndex(Type *Ty, Value *Idx);
  static Type *getTypeAtIndex(Type *Ty, uint64_t Idx);

  inline op_iterator       idx_begin()       { return op_begin()+1; }
  inline const_op_iterator idx_begin() const { return op_begin()+1; }
  inline op_iterator       idx_end()         { return op_end(); }
  inline const_op_iterator idx_end()   const { return op_end(); }

  inline iterator_range<op_iterator> indices() {
    return make_range(idx_begin(), idx_end());
  }

  inline iterator_range<const_op_iterator> indices() const {
    return make_range(idx_begin(), idx_end());
  }

  Value *getPointerOperand() {
    return getOperand(0);
  }
  const Value *getPointerOperand() const {
    return getOperand(0);
  }
  static unsigned getPointerOperandIndex() {
    return 0U;    // get index for modifying correct operand.
  }

  /// Method to return the pointer operand as a
  /// PointerType.
  Type *getPointerOperandType() const {
    return getPointerOperand()->getType();
  }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperandType()->getPointerAddressSpace();
  }

  /// Returns the pointer type returned by the GEP
  /// instruction, which may be a vector of pointers.
  static Type *getGEPReturnType(Value *Ptr, ArrayRef<Value *> IdxList) {
    // Vector GEP
    Type *Ty = Ptr->getType();
    if (Ty->isVectorTy())
      return Ty;

    for (Value *Index : IdxList)
      if (auto *IndexVTy = dyn_cast<VectorType>(Index->getType())) {
        ElementCount EltCount = IndexVTy->getElementCount();
        return VectorType::get(Ty, EltCount);
      }
    // Scalar GEP
    return Ty;
  }

  unsigned getNumIndices() const {  // Note: always non-negative
    return getNumOperands() - 1;
  }

  bool hasIndices() const {
    return getNumOperands() > 1;
  }

  /// Return true if all of the indices of this GEP are
  /// zeros.  If so, the result pointer and the first operand have the same
  /// value, just potentially different types.
  bool hasAllZeroIndices() const;

  /// Return true if all of the indices of this GEP are
  /// constant integers.  If so, the result pointer and the first operand have
  /// a constant offset between them.
  bool hasAllConstantIndices() const;

  /// Set nowrap flags for GEP instruction.
  void setNoWrapFlags(GEPNoWrapFlags NW);

  /// Set or clear the inbounds flag on this GEP instruction.
  /// See LangRef.html for the meaning of inbounds on a getelementptr.
  /// TODO: Remove this method in favor of setNoWrapFlags().
  void setIsInBounds(bool b = true);

  /// Get the nowrap flags for the GEP instruction.
  GEPNoWrapFlags getNoWrapFlags() const;

  /// Determine whether the GEP has the inbounds flag.
  bool isInBounds() const;

  /// Determine whether the GEP has the nusw flag.
  bool hasNoUnsignedSignedWrap() const;

  /// Determine whether the GEP has the nuw flag.
  bool hasNoUnsignedWrap() const;

  /// Accumulate the constant address offset of this GEP if possible.
  ///
  /// This routine accepts an APInt into which it will accumulate the constant
  /// offset of this GEP if the GEP is in fact constant. If the GEP is not
  /// all-constant, it returns false and the value of the offset APInt is
  /// undefined (it is *not* preserved!). The APInt passed into this routine
  /// must be at least as wide as the IntPtr type for the address space of
  /// the base GEP pointer.
  bool accumulateConstantOffset(const DataLayout &DL, APInt &Offset) const;
  bool collectOffset(const DataLayout &DL, unsigned BitWidth,
                     MapVector<Value *, APInt> &VariableOffsets,
                     APInt &ConstantOffset) const;
  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::GetElementPtr);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<GetElementPtrInst> :
  public VariadicOperandTraits<GetElementPtrInst, 1> {
};

GetElementPtrInst::GetElementPtrInst(Type *PointeeType, Value *Ptr,
                                     ArrayRef<Value *> IdxList, unsigned Values,
                                     const Twine &NameStr,
                                     InsertPosition InsertBefore)
    : Instruction(getGEPReturnType(Ptr, IdxList), GetElementPtr,
                  OperandTraits<GetElementPtrInst>::op_end(this) - Values,
                  Values, InsertBefore),
      SourceElementType(PointeeType),
      ResultElementType(getIndexedType(PointeeType, IdxList)) {
  init(Ptr, IdxList, NameStr);
}

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(GetElementPtrInst, Value)

//===----------------------------------------------------------------------===//
//                               ICmpInst Class
//===----------------------------------------------------------------------===//

/// This instruction compares its operands according to the predicate given
/// to the constructor. It only operates on integers or pointers. The operands
/// must be identical types.
/// Represent an integer comparison operator.
class ICmpInst: public CmpInst {
  void AssertOK() {
    assert(isIntPredicate() &&
           "Invalid ICmp predicate value");
    assert(getOperand(0)->getType() == getOperand(1)->getType() &&
          "Both operands to ICmp instruction are not of the same type!");
    // Check that the operands are the right type
    assert((getOperand(0)->getType()->isIntOrIntVectorTy() ||
            getOperand(0)->getType()->isPtrOrPtrVectorTy()) &&
           "Invalid operand types for ICmp instruction");
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical ICmpInst
  ICmpInst *cloneImpl() const;

public:
  /// Constructor with insertion semantics.
  ICmpInst(InsertPosition InsertBefore, ///< Where to insert
           Predicate pred, ///< The predicate to use for the comparison
           Value *LHS,     ///< The left-hand-side of the expression
           Value *RHS,     ///< The right-hand-side of the expression
           const Twine &NameStr = "" ///< Name of the instruction
           )
      : CmpInst(makeCmpResultType(LHS->getType()), Instruction::ICmp, pred, LHS,
                RHS, NameStr, InsertBefore) {
#ifndef NDEBUG
  AssertOK();
#endif
  }

  /// Constructor with no-insertion semantics
  ICmpInst(
    Predicate pred, ///< The predicate to use for the comparison
    Value *LHS,     ///< The left-hand-side of the expression
    Value *RHS,     ///< The right-hand-side of the expression
    const Twine &NameStr = "" ///< Name of the instruction
  ) : CmpInst(makeCmpResultType(LHS->getType()),
              Instruction::ICmp, pred, LHS, RHS, NameStr) {
#ifndef NDEBUG
  AssertOK();
#endif
  }

  /// For example, EQ->EQ, SLE->SLE, UGT->SGT, etc.
  /// @returns the predicate that would be the result if the operand were
  /// regarded as signed.
  /// Return the signed version of the predicate
  Predicate getSignedPredicate() const {
    return getSignedPredicate(getPredicate());
  }

  /// This is a static version that you can use without an instruction.
  /// Return the signed version of the predicate.
  static Predicate getSignedPredicate(Predicate pred);

  /// For example, EQ->EQ, SLE->ULE, UGT->UGT, etc.
  /// @returns the predicate that would be the result if the operand were
  /// regarded as unsigned.
  /// Return the unsigned version of the predicate
  Predicate getUnsignedPredicate() const {
    return getUnsignedPredicate(getPredicate());
  }

  /// This is a static version that you can use without an instruction.
  /// Return the unsigned version of the predicate.
  static Predicate getUnsignedPredicate(Predicate pred);

  /// Return true if this predicate is either EQ or NE.  This also
  /// tests for commutativity.
  static bool isEquality(Predicate P) {
    return P == ICMP_EQ || P == ICMP_NE;
  }

  /// Return true if this predicate is either EQ or NE.  This also
  /// tests for commutativity.
  bool isEquality() const {
    return isEquality(getPredicate());
  }

  /// @returns true if the predicate of this ICmpInst is commutative
  /// Determine if this relation is commutative.
  bool isCommutative() const { return isEquality(); }

  /// Return true if the predicate is relational (not EQ or NE).
  ///
  bool isRelational() const {
    return !isEquality();
  }

  /// Return true if the predicate is relational (not EQ or NE).
  ///
  static bool isRelational(Predicate P) {
    return !isEquality(P);
  }

  /// Return true if the predicate is SGT or UGT.
  ///
  static bool isGT(Predicate P) {
    return P == ICMP_SGT || P == ICMP_UGT;
  }

  /// Return true if the predicate is SLT or ULT.
  ///
  static bool isLT(Predicate P) {
    return P == ICMP_SLT || P == ICMP_ULT;
  }

  /// Return true if the predicate is SGE or UGE.
  ///
  static bool isGE(Predicate P) {
    return P == ICMP_SGE || P == ICMP_UGE;
  }

  /// Return true if the predicate is SLE or ULE.
  ///
  static bool isLE(Predicate P) {
    return P == ICMP_SLE || P == ICMP_ULE;
  }

  /// Returns the sequence of all ICmp predicates.
  ///
  static auto predicates() { return ICmpPredicates(); }

  /// Exchange the two operands to this instruction in such a way that it does
  /// not modify the semantics of the instruction. The predicate value may be
  /// changed to retain the same result if the predicate is order dependent
  /// (e.g. ult).
  /// Swap operands and adjust predicate.
  void swapOperands() {
    setPredicate(getSwappedPredicate());
    Op<0>().swap(Op<1>());
  }

  /// Return result of `LHS Pred RHS` comparison.
  static bool compare(const APInt &LHS, const APInt &RHS,
                      ICmpInst::Predicate Pred);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ICmp;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                               FCmpInst Class
//===----------------------------------------------------------------------===//

/// This instruction compares its operands according to the predicate given
/// to the constructor. It only operates on floating point values or packed
/// vectors of floating point values. The operands must be identical types.
/// Represents a floating point comparison operator.
class FCmpInst: public CmpInst {
  void AssertOK() {
    assert(isFPPredicate() && "Invalid FCmp predicate value");
    assert(getOperand(0)->getType() == getOperand(1)->getType() &&
           "Both operands to FCmp instruction are not of the same type!");
    // Check that the operands are the right type
    assert(getOperand(0)->getType()->isFPOrFPVectorTy() &&
           "Invalid operand types for FCmp instruction");
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FCmpInst
  FCmpInst *cloneImpl() const;

public:
  /// Constructor with insertion semantics.
  FCmpInst(InsertPosition InsertBefore, ///< Where to insert
           Predicate pred, ///< The predicate to use for the comparison
           Value *LHS,     ///< The left-hand-side of the expression
           Value *RHS,     ///< The right-hand-side of the expression
           const Twine &NameStr = "" ///< Name of the instruction
           )
      : CmpInst(makeCmpResultType(LHS->getType()), Instruction::FCmp, pred, LHS,
                RHS, NameStr, InsertBefore) {
    AssertOK();
  }

  /// Constructor with no-insertion semantics
  FCmpInst(Predicate Pred, ///< The predicate to use for the comparison
           Value *LHS,     ///< The left-hand-side of the expression
           Value *RHS,     ///< The right-hand-side of the expression
           const Twine &NameStr = "", ///< Name of the instruction
           Instruction *FlagsSource = nullptr)
      : CmpInst(makeCmpResultType(LHS->getType()), Instruction::FCmp, Pred, LHS,
                RHS, NameStr, nullptr, FlagsSource) {
    AssertOK();
  }

  /// @returns true if the predicate of this instruction is EQ or NE.
  /// Determine if this is an equality predicate.
  static bool isEquality(Predicate Pred) {
    return Pred == FCMP_OEQ || Pred == FCMP_ONE || Pred == FCMP_UEQ ||
           Pred == FCMP_UNE;
  }

  /// @returns true if the predicate of this instruction is EQ or NE.
  /// Determine if this is an equality predicate.
  bool isEquality() const { return isEquality(getPredicate()); }

  /// @returns true if the predicate of this instruction is commutative.
  /// Determine if this is a commutative predicate.
  bool isCommutative() const {
    return isEquality() ||
           getPredicate() == FCMP_FALSE ||
           getPredicate() == FCMP_TRUE ||
           getPredicate() == FCMP_ORD ||
           getPredicate() == FCMP_UNO;
  }

  /// @returns true if the predicate is relational (not EQ or NE).
  /// Determine if this a relational predicate.
  bool isRelational() const { return !isEquality(); }

  /// Exchange the two operands to this instruction in such a way that it does
  /// not modify the semantics of the instruction. The predicate value may be
  /// changed to retain the same result if the predicate is order dependent
  /// (e.g. ult).
  /// Swap operands and adjust predicate.
  void swapOperands() {
    setPredicate(getSwappedPredicate());
    Op<0>().swap(Op<1>());
  }

  /// Returns the sequence of all FCmp predicates.
  ///
  static auto predicates() { return FCmpPredicates(); }

  /// Return result of `LHS Pred RHS` comparison.
  static bool compare(const APFloat &LHS, const APFloat &RHS,
                      FCmpInst::Predicate Pred);

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::FCmp;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
/// This class represents a function call, abstracting a target
/// machine's calling convention.  This class uses low bit of the SubClassData
/// field to indicate whether or not this is a tail call.  The rest of the bits
/// hold the calling convention of the call.
///
class CallInst : public CallBase {
  CallInst(const CallInst &CI);

  /// Construct a CallInst from a range of arguments
  inline CallInst(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                  ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr,
                  InsertPosition InsertBefore);

  inline CallInst(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                  const Twine &NameStr, InsertPosition InsertBefore)
      : CallInst(Ty, Func, Args, std::nullopt, NameStr, InsertBefore) {}

  explicit CallInst(FunctionType *Ty, Value *F, const Twine &NameStr,
                    InsertPosition InsertBefore);

  void init(FunctionType *FTy, Value *Func, ArrayRef<Value *> Args,
            ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr);
  void init(FunctionType *FTy, Value *Func, const Twine &NameStr);

  /// Compute the number of operands to allocate.
  static int ComputeNumOperands(int NumArgs, int NumBundleInputs = 0) {
    // We need one operand for the called function, plus the input operand
    // counts provided.
    return 1 + NumArgs + NumBundleInputs;
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  CallInst *cloneImpl() const;

public:
  static CallInst *Create(FunctionType *Ty, Value *F, const Twine &NameStr = "",
                          InsertPosition InsertBefore = nullptr) {
    return new (ComputeNumOperands(0)) CallInst(Ty, F, NameStr, InsertBefore);
  }

  static CallInst *Create(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                          const Twine &NameStr,
                          InsertPosition InsertBefore = nullptr) {
    return new (ComputeNumOperands(Args.size()))
        CallInst(Ty, Func, Args, std::nullopt, NameStr, InsertBefore);
  }

  static CallInst *Create(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                          ArrayRef<OperandBundleDef> Bundles = std::nullopt,
                          const Twine &NameStr = "",
                          InsertPosition InsertBefore = nullptr) {
    const int NumOperands =
        ComputeNumOperands(Args.size(), CountBundleInputs(Bundles));
    const unsigned DescriptorBytes = Bundles.size() * sizeof(BundleOpInfo);

    return new (NumOperands, DescriptorBytes)
        CallInst(Ty, Func, Args, Bundles, NameStr, InsertBefore);
  }

  static CallInst *Create(FunctionCallee Func, const Twine &NameStr = "",
                          InsertPosition InsertBefore = nullptr) {
    return Create(Func.getFunctionType(), Func.getCallee(), NameStr,
                  InsertBefore);
  }

  static CallInst *Create(FunctionCallee Func, ArrayRef<Value *> Args,
                          ArrayRef<OperandBundleDef> Bundles = std::nullopt,
                          const Twine &NameStr = "",
                          InsertPosition InsertBefore = nullptr) {
    return Create(Func.getFunctionType(), Func.getCallee(), Args, Bundles,
                  NameStr, InsertBefore);
  }

  static CallInst *Create(FunctionCallee Func, ArrayRef<Value *> Args,
                          const Twine &NameStr,
                          InsertPosition InsertBefore = nullptr) {
    return Create(Func.getFunctionType(), Func.getCallee(), Args, NameStr,
                  InsertBefore);
  }

  /// Create a clone of \p CI with a different set of operand bundles and
  /// insert it before \p InsertBefore.
  ///
  /// The returned call instruction is identical \p CI in every way except that
  /// the operand bundles for the new instruction are set to the operand bundles
  /// in \p Bundles.
  static CallInst *Create(CallInst *CI, ArrayRef<OperandBundleDef> Bundles,
                          InsertPosition InsertPt = nullptr);

  // Note that 'musttail' implies 'tail'.
  enum TailCallKind : unsigned {
    TCK_None = 0,
    TCK_Tail = 1,
    TCK_MustTail = 2,
    TCK_NoTail = 3,
    TCK_LAST = TCK_NoTail
  };

  using TailCallKindField = Bitfield::Element<TailCallKind, 0, 2, TCK_LAST>;
  static_assert(
      Bitfield::areContiguous<TailCallKindField, CallBase::CallingConvField>(),
      "Bitfields must be contiguous");

  TailCallKind getTailCallKind() const {
    return getSubclassData<TailCallKindField>();
  }

  bool isTailCall() const {
    TailCallKind Kind = getTailCallKind();
    return Kind == TCK_Tail || Kind == TCK_MustTail;
  }

  bool isMustTailCall() const { return getTailCallKind() == TCK_MustTail; }

  bool isNoTailCall() const { return getTailCallKind() == TCK_NoTail; }

  void setTailCallKind(TailCallKind TCK) {
    setSubclassData<TailCallKindField>(TCK);
  }

  void setTailCall(bool IsTc = true) {
    setTailCallKind(IsTc ? TCK_Tail : TCK_None);
  }

  /// Return true if the call can return twice
  bool canReturnTwice() const { return hasFnAttr(Attribute::ReturnsTwice); }
  void setCanReturnTwice() { addFnAttr(Attribute::ReturnsTwice); }

  /// Return true if the call is for a noreturn trap intrinsic.
  bool isNonContinuableTrap() const {
    switch (getIntrinsicID()) {
    case Intrinsic::trap:
    case Intrinsic::ubsantrap:
      return !hasFnAttr("trap-func-name");
    default:
      return false;
    }
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Call;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

  /// Updates profile metadata by scaling it by \p S / \p T.
  void updateProfWeight(uint64_t S, uint64_t T);

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  template <typename Bitfield>
  void setSubclassData(typename Bitfield::Type Value) {
    Instruction::setSubclassData<Bitfield>(Value);
  }
};

CallInst::CallInst(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                   ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr,
                   InsertPosition InsertBefore)
    : CallBase(Ty->getReturnType(), Instruction::Call,
               OperandTraits<CallBase>::op_end(this) -
                   (Args.size() + CountBundleInputs(Bundles) + 1),
               unsigned(Args.size() + CountBundleInputs(Bundles) + 1),
               InsertBefore) {
  init(Ty, Func, Args, Bundles, NameStr);
}

//===----------------------------------------------------------------------===//
//                               SelectInst Class
//===----------------------------------------------------------------------===//

/// This class represents the LLVM 'select' instruction.
///
class SelectInst : public Instruction {

  SelectInst(Value *C, Value *S1, Value *S2, const Twine &NameStr,
             InsertPosition InsertBefore)
      : Instruction(S1->getType(), Instruction::Select, &Op<0>(), 3,
                    InsertBefore) {
    init(C, S1, S2);
    setName(NameStr);
  }

  void init(Value *C, Value *S1, Value *S2) {
    assert(!areInvalidOperands(C, S1, S2) && "Invalid operands for select");
    Op<0>() = C;
    Op<1>() = S1;
    Op<2>() = S2;
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  SelectInst *cloneImpl() const;

public:
  static SelectInst *Create(Value *C, Value *S1, Value *S2,
                            const Twine &NameStr = "",
                            InsertPosition InsertBefore = nullptr,
                            Instruction *MDFrom = nullptr) {
    SelectInst *Sel = new(3) SelectInst(C, S1, S2, NameStr, InsertBefore);
    if (MDFrom)
      Sel->copyMetadata(*MDFrom);
    return Sel;
  }

  const Value *getCondition() const { return Op<0>(); }
  const Value *getTrueValue() const { return Op<1>(); }
  const Value *getFalseValue() const { return Op<2>(); }
  Value *getCondition() { return Op<0>(); }
  Value *getTrueValue() { return Op<1>(); }
  Value *getFalseValue() { return Op<2>(); }

  void setCondition(Value *V) { Op<0>() = V; }
  void setTrueValue(Value *V) { Op<1>() = V; }
  void setFalseValue(Value *V) { Op<2>() = V; }

  /// Swap the true and false values of the select instruction.
  /// This doesn't swap prof metadata.
  void swapValues() { Op<1>().swap(Op<2>()); }

  /// Return a string if the specified operands are invalid
  /// for a select operation, otherwise return null.
  static const char *areInvalidOperands(Value *Cond, Value *True, Value *False);

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  OtherOps getOpcode() const {
    return static_cast<OtherOps>(Instruction::getOpcode());
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Select;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<SelectInst> : public FixedNumOperandTraits<SelectInst, 3> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(SelectInst, Value)

//===----------------------------------------------------------------------===//
//                                VAArgInst Class
//===----------------------------------------------------------------------===//

/// This class represents the va_arg llvm instruction, which returns
/// an argument of the specified type given a va_list and increments that list
///
class VAArgInst : public UnaryInstruction {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  VAArgInst *cloneImpl() const;

public:
  VAArgInst(Value *List, Type *Ty, const Twine &NameStr = "",
            InsertPosition InsertBefore = nullptr)
      : UnaryInstruction(Ty, VAArg, List, InsertBefore) {
    setName(NameStr);
  }

  Value *getPointerOperand() { return getOperand(0); }
  const Value *getPointerOperand() const { return getOperand(0); }
  static unsigned getPointerOperandIndex() { return 0U; }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == VAArg;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                ExtractElementInst Class
//===----------------------------------------------------------------------===//

/// This instruction extracts a single (scalar)
/// element from a VectorType value
///
class ExtractElementInst : public Instruction {
  ExtractElementInst(Value *Vec, Value *Idx, const Twine &NameStr = "",
                     InsertPosition InsertBefore = nullptr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  ExtractElementInst *cloneImpl() const;

public:
  static ExtractElementInst *Create(Value *Vec, Value *Idx,
                                    const Twine &NameStr = "",
                                    InsertPosition InsertBefore = nullptr) {
    return new(2) ExtractElementInst(Vec, Idx, NameStr, InsertBefore);
  }

  /// Return true if an extractelement instruction can be
  /// formed with the specified operands.
  static bool isValidOperands(const Value *Vec, const Value *Idx);

  Value *getVectorOperand() { return Op<0>(); }
  Value *getIndexOperand() { return Op<1>(); }
  const Value *getVectorOperand() const { return Op<0>(); }
  const Value *getIndexOperand() const { return Op<1>(); }

  VectorType *getVectorOperandType() const {
    return cast<VectorType>(getVectorOperand()->getType());
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ExtractElement;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<ExtractElementInst> :
  public FixedNumOperandTraits<ExtractElementInst, 2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ExtractElementInst, Value)

//===----------------------------------------------------------------------===//
//                                InsertElementInst Class
//===----------------------------------------------------------------------===//

/// This instruction inserts a single (scalar)
/// element into a VectorType value
///
class InsertElementInst : public Instruction {
  InsertElementInst(Value *Vec, Value *NewElt, Value *Idx,
                    const Twine &NameStr = "",
                    InsertPosition InsertBefore = nullptr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  InsertElementInst *cloneImpl() const;

public:
  static InsertElementInst *Create(Value *Vec, Value *NewElt, Value *Idx,
                                   const Twine &NameStr = "",
                                   InsertPosition InsertBefore = nullptr) {
    return new(3) InsertElementInst(Vec, NewElt, Idx, NameStr, InsertBefore);
  }

  /// Return true if an insertelement instruction can be
  /// formed with the specified operands.
  static bool isValidOperands(const Value *Vec, const Value *NewElt,
                              const Value *Idx);

  /// Overload to return most specific vector type.
  ///
  VectorType *getType() const {
    return cast<VectorType>(Instruction::getType());
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::InsertElement;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<InsertElementInst> :
  public FixedNumOperandTraits<InsertElementInst, 3> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(InsertElementInst, Value)

//===----------------------------------------------------------------------===//
//                           ShuffleVectorInst Class
//===----------------------------------------------------------------------===//

constexpr int PoisonMaskElem = -1;

/// This instruction constructs a fixed permutation of two
/// input vectors.
///
/// For each element of the result vector, the shuffle mask selects an element
/// from one of the input vectors to copy to the result. Non-negative elements
/// in the mask represent an index into the concatenated pair of input vectors.
/// PoisonMaskElem (-1) specifies that the result element is poison.
///
/// For scalable vectors, all the elements of the mask must be 0 or -1. This
/// requirement may be relaxed in the future.
class ShuffleVectorInst : public Instruction {
  SmallVector<int, 4> ShuffleMask;
  Constant *ShuffleMaskForBitcode;

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  ShuffleVectorInst *cloneImpl() const;

public:
  ShuffleVectorInst(Value *V1, Value *Mask, const Twine &NameStr = "",
                    InsertPosition InsertBefore = nullptr);
  ShuffleVectorInst(Value *V1, ArrayRef<int> Mask, const Twine &NameStr = "",
                    InsertPosition InsertBefore = nullptr);
  ShuffleVectorInst(Value *V1, Value *V2, Value *Mask,
                    const Twine &NameStr = "",
                    InsertPosition InsertBefore = nullptr);
  ShuffleVectorInst(Value *V1, Value *V2, ArrayRef<int> Mask,
                    const Twine &NameStr = "",
                    InsertPosition InsertBefore = nullptr);

  void *operator new(size_t S) { return User::operator new(S, 2); }
  void operator delete(void *Ptr) { return User::operator delete(Ptr); }

  /// Swap the operands and adjust the mask to preserve the semantics
  /// of the instruction.
  void commute();

  /// Return true if a shufflevector instruction can be
  /// formed with the specified operands.
  static bool isValidOperands(const Value *V1, const Value *V2,
                              const Value *Mask);
  static bool isValidOperands(const Value *V1, const Value *V2,
                              ArrayRef<int> Mask);

  /// Overload to return most specific vector type.
  ///
  VectorType *getType() const {
    return cast<VectorType>(Instruction::getType());
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Return the shuffle mask value of this instruction for the given element
  /// index. Return PoisonMaskElem if the element is undef.
  int getMaskValue(unsigned Elt) const { return ShuffleMask[Elt]; }

  /// Convert the input shuffle mask operand to a vector of integers. Undefined
  /// elements of the mask are returned as PoisonMaskElem.
  static void getShuffleMask(const Constant *Mask,
                             SmallVectorImpl<int> &Result);

  /// Return the mask for this instruction as a vector of integers. Undefined
  /// elements of the mask are returned as PoisonMaskElem.
  void getShuffleMask(SmallVectorImpl<int> &Result) const {
    Result.assign(ShuffleMask.begin(), ShuffleMask.end());
  }

  /// Return the mask for this instruction, for use in bitcode.
  ///
  /// TODO: This is temporary until we decide a new bitcode encoding for
  /// shufflevector.
  Constant *getShuffleMaskForBitcode() const { return ShuffleMaskForBitcode; }

  static Constant *convertShuffleMaskForBitcode(ArrayRef<int> Mask,
                                                Type *ResultTy);

  void setShuffleMask(ArrayRef<int> Mask);

  ArrayRef<int> getShuffleMask() const { return ShuffleMask; }

  /// Return true if this shuffle returns a vector with a different number of
  /// elements than its source vectors.
  /// Examples: shufflevector <4 x n> A, <4 x n> B, <1,2,3>
  ///           shufflevector <4 x n> A, <4 x n> B, <1,2,3,4,5>
  bool changesLength() const {
    unsigned NumSourceElts = cast<VectorType>(Op<0>()->getType())
                                 ->getElementCount()
                                 .getKnownMinValue();
    unsigned NumMaskElts = ShuffleMask.size();
    return NumSourceElts != NumMaskElts;
  }

  /// Return true if this shuffle returns a vector with a greater number of
  /// elements than its source vectors.
  /// Example: shufflevector <2 x n> A, <2 x n> B, <1,2,3>
  bool increasesLength() const {
    unsigned NumSourceElts = cast<VectorType>(Op<0>()->getType())
                                 ->getElementCount()
                                 .getKnownMinValue();
    unsigned NumMaskElts = ShuffleMask.size();
    return NumSourceElts < NumMaskElts;
  }

  /// Return true if this shuffle mask chooses elements from exactly one source
  /// vector.
  /// Example: <7,5,undef,7>
  /// This assumes that vector operands (of length \p NumSrcElts) are the same
  /// length as the mask.
  static bool isSingleSourceMask(ArrayRef<int> Mask, int NumSrcElts);
  static bool isSingleSourceMask(const Constant *Mask, int NumSrcElts) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isSingleSourceMask(MaskAsInts, NumSrcElts);
  }

  /// Return true if this shuffle chooses elements from exactly one source
  /// vector without changing the length of that vector.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <3,0,undef,3>
  /// TODO: Optionally allow length-changing shuffles.
  bool isSingleSource() const {
    return !changesLength() &&
           isSingleSourceMask(ShuffleMask, ShuffleMask.size());
  }

  /// Return true if this shuffle mask chooses elements from exactly one source
  /// vector without lane crossings. A shuffle using this mask is not
  /// necessarily a no-op because it may change the number of elements from its
  /// input vectors or it may provide demanded bits knowledge via undef lanes.
  /// Example: <undef,undef,2,3>
  static bool isIdentityMask(ArrayRef<int> Mask, int NumSrcElts);
  static bool isIdentityMask(const Constant *Mask, int NumSrcElts) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");

    // Not possible to express a shuffle mask for a scalable vector for this
    // case.
    if (isa<ScalableVectorType>(Mask->getType()))
      return false;

    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isIdentityMask(MaskAsInts, NumSrcElts);
  }

  /// Return true if this shuffle chooses elements from exactly one source
  /// vector without lane crossings and does not change the number of elements
  /// from its input vectors.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <4,undef,6,undef>
  bool isIdentity() const {
    // Not possible to express a shuffle mask for a scalable vector for this
    // case.
    if (isa<ScalableVectorType>(getType()))
      return false;

    return !changesLength() && isIdentityMask(ShuffleMask, ShuffleMask.size());
  }

  /// Return true if this shuffle lengthens exactly one source vector with
  /// undefs in the high elements.
  bool isIdentityWithPadding() const;

  /// Return true if this shuffle extracts the first N elements of exactly one
  /// source vector.
  bool isIdentityWithExtract() const;

  /// Return true if this shuffle concatenates its 2 source vectors. This
  /// returns false if either input is undefined. In that case, the shuffle is
  /// is better classified as an identity with padding operation.
  bool isConcat() const;

  /// Return true if this shuffle mask chooses elements from its source vectors
  /// without lane crossings. A shuffle using this mask would be
  /// equivalent to a vector select with a constant condition operand.
  /// Example: <4,1,6,undef>
  /// This returns false if the mask does not choose from both input vectors.
  /// In that case, the shuffle is better classified as an identity shuffle.
  /// This assumes that vector operands are the same length as the mask
  /// (a length-changing shuffle can never be equivalent to a vector select).
  static bool isSelectMask(ArrayRef<int> Mask, int NumSrcElts);
  static bool isSelectMask(const Constant *Mask, int NumSrcElts) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isSelectMask(MaskAsInts, NumSrcElts);
  }

  /// Return true if this shuffle chooses elements from its source vectors
  /// without lane crossings and all operands have the same number of elements.
  /// In other words, this shuffle is equivalent to a vector select with a
  /// constant condition operand.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <undef,1,6,3>
  /// This returns false if the mask does not choose from both input vectors.
  /// In that case, the shuffle is better classified as an identity shuffle.
  /// TODO: Optionally allow length-changing shuffles.
  bool isSelect() const {
    return !changesLength() && isSelectMask(ShuffleMask, ShuffleMask.size());
  }

  /// Return true if this shuffle mask swaps the order of elements from exactly
  /// one source vector.
  /// Example: <7,6,undef,4>
  /// This assumes that vector operands (of length \p NumSrcElts) are the same
  /// length as the mask.
  static bool isReverseMask(ArrayRef<int> Mask, int NumSrcElts);
  static bool isReverseMask(const Constant *Mask, int NumSrcElts) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isReverseMask(MaskAsInts, NumSrcElts);
  }

  /// Return true if this shuffle swaps the order of elements from exactly
  /// one source vector.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <3,undef,1,undef>
  /// TODO: Optionally allow length-changing shuffles.
  bool isReverse() const {
    return !changesLength() && isReverseMask(ShuffleMask, ShuffleMask.size());
  }

  /// Return true if this shuffle mask chooses all elements with the same value
  /// as the first element of exactly one source vector.
  /// Example: <4,undef,undef,4>
  /// This assumes that vector operands (of length \p NumSrcElts) are the same
  /// length as the mask.
  static bool isZeroEltSplatMask(ArrayRef<int> Mask, int NumSrcElts);
  static bool isZeroEltSplatMask(const Constant *Mask, int NumSrcElts) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isZeroEltSplatMask(MaskAsInts, NumSrcElts);
  }

  /// Return true if all elements of this shuffle are the same value as the
  /// first element of exactly one source vector without changing the length
  /// of that vector.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <undef,0,undef,0>
  /// TODO: Optionally allow length-changing shuffles.
  /// TODO: Optionally allow splats from other elements.
  bool isZeroEltSplat() const {
    return !changesLength() &&
           isZeroEltSplatMask(ShuffleMask, ShuffleMask.size());
  }

  /// Return true if this shuffle mask is a transpose mask.
  /// Transpose vector masks transpose a 2xn matrix. They read corresponding
  /// even- or odd-numbered vector elements from two n-dimensional source
  /// vectors and write each result into consecutive elements of an
  /// n-dimensional destination vector. Two shuffles are necessary to complete
  /// the transpose, one for the even elements and another for the odd elements.
  /// This description closely follows how the TRN1 and TRN2 AArch64
  /// instructions operate.
  ///
  /// For example, a simple 2x2 matrix can be transposed with:
  ///
  ///   ; Original matrix
  ///   m0 = < a, b >
  ///   m1 = < c, d >
  ///
  ///   ; Transposed matrix
  ///   t0 = < a, c > = shufflevector m0, m1, < 0, 2 >
  ///   t1 = < b, d > = shufflevector m0, m1, < 1, 3 >
  ///
  /// For matrices having greater than n columns, the resulting nx2 transposed
  /// matrix is stored in two result vectors such that one vector contains
  /// interleaved elements from all the even-numbered rows and the other vector
  /// contains interleaved elements from all the odd-numbered rows. For example,
  /// a 2x4 matrix can be transposed with:
  ///
  ///   ; Original matrix
  ///   m0 = < a, b, c, d >
  ///   m1 = < e, f, g, h >
  ///
  ///   ; Transposed matrix
  ///   t0 = < a, e, c, g > = shufflevector m0, m1 < 0, 4, 2, 6 >
  ///   t1 = < b, f, d, h > = shufflevector m0, m1 < 1, 5, 3, 7 >
  static bool isTransposeMask(ArrayRef<int> Mask, int NumSrcElts);
  static bool isTransposeMask(const Constant *Mask, int NumSrcElts) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isTransposeMask(MaskAsInts, NumSrcElts);
  }

  /// Return true if this shuffle transposes the elements of its inputs without
  /// changing the length of the vectors. This operation may also be known as a
  /// merge or interleave. See the description for isTransposeMask() for the
  /// exact specification.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <0,4,2,6>
  bool isTranspose() const {
    return !changesLength() && isTransposeMask(ShuffleMask, ShuffleMask.size());
  }

  /// Return true if this shuffle mask is a splice mask, concatenating the two
  /// inputs together and then extracts an original width vector starting from
  /// the splice index.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <1,2,3,4>
  /// This assumes that vector operands (of length \p NumSrcElts) are the same
  /// length as the mask.
  static bool isSpliceMask(ArrayRef<int> Mask, int NumSrcElts, int &Index);
  static bool isSpliceMask(const Constant *Mask, int NumSrcElts, int &Index) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isSpliceMask(MaskAsInts, NumSrcElts, Index);
  }

  /// Return true if this shuffle splices two inputs without changing the length
  /// of the vectors. This operation concatenates the two inputs together and
  /// then extracts an original width vector starting from the splice index.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <1,2,3,4>
  bool isSplice(int &Index) const {
    return !changesLength() &&
           isSpliceMask(ShuffleMask, ShuffleMask.size(), Index);
  }

  /// Return true if this shuffle mask is an extract subvector mask.
  /// A valid extract subvector mask returns a smaller vector from a single
  /// source operand. The base extraction index is returned as well.
  static bool isExtractSubvectorMask(ArrayRef<int> Mask, int NumSrcElts,
                                     int &Index);
  static bool isExtractSubvectorMask(const Constant *Mask, int NumSrcElts,
                                     int &Index) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    // Not possible to express a shuffle mask for a scalable vector for this
    // case.
    if (isa<ScalableVectorType>(Mask->getType()))
      return false;
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isExtractSubvectorMask(MaskAsInts, NumSrcElts, Index);
  }

  /// Return true if this shuffle mask is an extract subvector mask.
  bool isExtractSubvectorMask(int &Index) const {
    // Not possible to express a shuffle mask for a scalable vector for this
    // case.
    if (isa<ScalableVectorType>(getType()))
      return false;

    int NumSrcElts =
        cast<FixedVectorType>(Op<0>()->getType())->getNumElements();
    return isExtractSubvectorMask(ShuffleMask, NumSrcElts, Index);
  }

  /// Return true if this shuffle mask is an insert subvector mask.
  /// A valid insert subvector mask inserts the lowest elements of a second
  /// source operand into an in-place first source operand.
  /// Both the sub vector width and the insertion index is returned.
  static bool isInsertSubvectorMask(ArrayRef<int> Mask, int NumSrcElts,
                                    int &NumSubElts, int &Index);
  static bool isInsertSubvectorMask(const Constant *Mask, int NumSrcElts,
                                    int &NumSubElts, int &Index) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    // Not possible to express a shuffle mask for a scalable vector for this
    // case.
    if (isa<ScalableVectorType>(Mask->getType()))
      return false;
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isInsertSubvectorMask(MaskAsInts, NumSrcElts, NumSubElts, Index);
  }

  /// Return true if this shuffle mask is an insert subvector mask.
  bool isInsertSubvectorMask(int &NumSubElts, int &Index) const {
    // Not possible to express a shuffle mask for a scalable vector for this
    // case.
    if (isa<ScalableVectorType>(getType()))
      return false;

    int NumSrcElts =
        cast<FixedVectorType>(Op<0>()->getType())->getNumElements();
    return isInsertSubvectorMask(ShuffleMask, NumSrcElts, NumSubElts, Index);
  }

  /// Return true if this shuffle mask replicates each of the \p VF elements
  /// in a vector \p ReplicationFactor times.
  /// For example, the mask for \p ReplicationFactor=3 and \p VF=4 is:
  ///   <0,0,0,1,1,1,2,2,2,3,3,3>
  static bool isReplicationMask(ArrayRef<int> Mask, int &ReplicationFactor,
                                int &VF);
  static bool isReplicationMask(const Constant *Mask, int &ReplicationFactor,
                                int &VF) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    // Not possible to express a shuffle mask for a scalable vector for this
    // case.
    if (isa<ScalableVectorType>(Mask->getType()))
      return false;
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isReplicationMask(MaskAsInts, ReplicationFactor, VF);
  }

  /// Return true if this shuffle mask is a replication mask.
  bool isReplicationMask(int &ReplicationFactor, int &VF) const;

  /// Return true if this shuffle mask represents "clustered" mask of size VF,
  /// i.e. each index between [0..VF) is used exactly once in each submask of
  /// size VF.
  /// For example, the mask for \p VF=4 is:
  /// 0, 1, 2, 3, 3, 2, 0, 1 - "clustered", because each submask of size 4
  /// (0,1,2,3 and 3,2,0,1) uses indices [0..VF) exactly one time.
  /// 0, 1, 2, 3, 3, 3, 1, 0 - not "clustered", because
  ///                          element 3 is used twice in the second submask
  ///                          (3,3,1,0) and index 2 is not used at all.
  static bool isOneUseSingleSourceMask(ArrayRef<int> Mask, int VF);

  /// Return true if this shuffle mask is a one-use-single-source("clustered")
  /// mask.
  bool isOneUseSingleSourceMask(int VF) const;

  /// Change values in a shuffle permute mask assuming the two vector operands
  /// of length InVecNumElts have swapped position.
  static void commuteShuffleMask(MutableArrayRef<int> Mask,
                                 unsigned InVecNumElts) {
    for (int &Idx : Mask) {
      if (Idx == -1)
        continue;
      Idx = Idx < (int)InVecNumElts ? Idx + InVecNumElts : Idx - InVecNumElts;
      assert(Idx >= 0 && Idx < (int)InVecNumElts * 2 &&
             "shufflevector mask index out of range");
    }
  }

  /// Return if this shuffle interleaves its two input vectors together.
  bool isInterleave(unsigned Factor);

  /// Return true if the mask interleaves one or more input vectors together.
  ///
  /// I.e. <0, LaneLen, ... , LaneLen*(Factor - 1), 1, LaneLen + 1, ...>
  /// E.g. For a Factor of 2 (LaneLen=4):
  ///   <0, 4, 1, 5, 2, 6, 3, 7>
  /// E.g. For a Factor of 3 (LaneLen=4):
  ///   <4, 0, 9, 5, 1, 10, 6, 2, 11, 7, 3, 12>
  /// E.g. For a Factor of 4 (LaneLen=2):
  ///   <0, 2, 6, 4, 1, 3, 7, 5>
  ///
  /// NumInputElts is the total number of elements in the input vectors.
  ///
  /// StartIndexes are the first indexes of each vector being interleaved,
  /// substituting any indexes that were undef
  /// E.g. <4, -1, 2, 5, 1, 3> (Factor=3): StartIndexes=<4, 0, 2>
  ///
  /// Note that this does not check if the input vectors are consecutive:
  /// It will return true for masks such as
  /// <0, 4, 6, 1, 5, 7> (Factor=3, LaneLen=2)
  static bool isInterleaveMask(ArrayRef<int> Mask, unsigned Factor,
                               unsigned NumInputElts,
                               SmallVectorImpl<unsigned> &StartIndexes);
  static bool isInterleaveMask(ArrayRef<int> Mask, unsigned Factor,
                               unsigned NumInputElts) {
    SmallVector<unsigned, 8> StartIndexes;
    return isInterleaveMask(Mask, Factor, NumInputElts, StartIndexes);
  }

  /// Check if the mask is a DE-interleave mask of the given factor
  /// \p Factor like:
  ///     <Index, Index+Factor, ..., Index+(NumElts-1)*Factor>
  static bool isDeInterleaveMaskOfFactor(ArrayRef<int> Mask, unsigned Factor,
                                         unsigned &Index);
  static bool isDeInterleaveMaskOfFactor(ArrayRef<int> Mask, unsigned Factor) {
    unsigned Unused;
    return isDeInterleaveMaskOfFactor(Mask, Factor, Unused);
  }

  /// Checks if the shuffle is a bit rotation of the first operand across
  /// multiple subelements, e.g:
  ///
  /// shuffle <8 x i8> %a, <8 x i8> poison, <8 x i32> <1, 0, 3, 2, 5, 4, 7, 6>
  ///
  /// could be expressed as
  ///
  /// rotl <4 x i16> %a, 8
  ///
  /// If it can be expressed as a rotation, returns the number of subelements to
  /// group by in NumSubElts and the number of bits to rotate left in RotateAmt.
  static bool isBitRotateMask(ArrayRef<int> Mask, unsigned EltSizeInBits,
                              unsigned MinSubElts, unsigned MaxSubElts,
                              unsigned &NumSubElts, unsigned &RotateAmt);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ShuffleVector;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<ShuffleVectorInst>
    : public FixedNumOperandTraits<ShuffleVectorInst, 2> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ShuffleVectorInst, Value)

//===----------------------------------------------------------------------===//
//                                ExtractValueInst Class
//===----------------------------------------------------------------------===//

/// This instruction extracts a struct member or array
/// element value from an aggregate value.
///
class ExtractValueInst : public UnaryInstruction {
  SmallVector<unsigned, 4> Indices;

  ExtractValueInst(const ExtractValueInst &EVI);

  /// Constructors - Create a extractvalue instruction with a base aggregate
  /// value and a list of indices. The first and second ctor can optionally
  /// insert before an existing instruction, the third appends the new
  /// instruction to the specified BasicBlock.
  inline ExtractValueInst(Value *Agg, ArrayRef<unsigned> Idxs,
                          const Twine &NameStr, InsertPosition InsertBefore);

  void init(ArrayRef<unsigned> Idxs, const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  ExtractValueInst *cloneImpl() const;

public:
  static ExtractValueInst *Create(Value *Agg, ArrayRef<unsigned> Idxs,
                                  const Twine &NameStr = "",
                                  InsertPosition InsertBefore = nullptr) {
    return new
      ExtractValueInst(Agg, Idxs, NameStr, InsertBefore);
  }

  /// Returns the type of the element that would be extracted
  /// with an extractvalue instruction with the specified parameters.
  ///
  /// Null is returned if the indices are invalid for the specified type.
  static Type *getIndexedType(Type *Agg, ArrayRef<unsigned> Idxs);

  using idx_iterator = const unsigned*;

  inline idx_iterator idx_begin() const { return Indices.begin(); }
  inline idx_iterator idx_end()   const { return Indices.end(); }
  inline iterator_range<idx_iterator> indices() const {
    return make_range(idx_begin(), idx_end());
  }

  Value *getAggregateOperand() {
    return getOperand(0);
  }
  const Value *getAggregateOperand() const {
    return getOperand(0);
  }
  static unsigned getAggregateOperandIndex() {
    return 0U;                      // get index for modifying correct operand
  }

  ArrayRef<unsigned> getIndices() const {
    return Indices;
  }

  unsigned getNumIndices() const {
    return (unsigned)Indices.size();
  }

  bool hasIndices() const {
    return true;
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ExtractValue;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

ExtractValueInst::ExtractValueInst(Value *Agg, ArrayRef<unsigned> Idxs,
                                   const Twine &NameStr,
                                   InsertPosition InsertBefore)
    : UnaryInstruction(checkGEPType(getIndexedType(Agg->getType(), Idxs)),
                       ExtractValue, Agg, InsertBefore) {
  init(Idxs, NameStr);
}

//===----------------------------------------------------------------------===//
//                                InsertValueInst Class
//===----------------------------------------------------------------------===//

/// This instruction inserts a struct field of array element
/// value into an aggregate value.
///
class InsertValueInst : public Instruction {
  SmallVector<unsigned, 4> Indices;

  InsertValueInst(const InsertValueInst &IVI);

  /// Constructors - Create a insertvalue instruction with a base aggregate
  /// value, a value to insert, and a list of indices. The first and second ctor
  /// can optionally insert before an existing instruction, the third appends
  /// the new instruction to the specified BasicBlock.
  inline InsertValueInst(Value *Agg, Value *Val, ArrayRef<unsigned> Idxs,
                         const Twine &NameStr, InsertPosition InsertBefore);

  /// Constructors - These three constructors are convenience methods because
  /// one and two index insertvalue instructions are so common.
  InsertValueInst(Value *Agg, Value *Val, unsigned Idx,
                  const Twine &NameStr = "",
                  InsertPosition InsertBefore = nullptr);

  void init(Value *Agg, Value *Val, ArrayRef<unsigned> Idxs,
            const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  InsertValueInst *cloneImpl() const;

public:
  // allocate space for exactly two operands
  void *operator new(size_t S) { return User::operator new(S, 2); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  static InsertValueInst *Create(Value *Agg, Value *Val,
                                 ArrayRef<unsigned> Idxs,
                                 const Twine &NameStr = "",
                                 InsertPosition InsertBefore = nullptr) {
    return new InsertValueInst(Agg, Val, Idxs, NameStr, InsertBefore);
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  using idx_iterator = const unsigned*;

  inline idx_iterator idx_begin() const { return Indices.begin(); }
  inline idx_iterator idx_end()   const { return Indices.end(); }
  inline iterator_range<idx_iterator> indices() const {
    return make_range(idx_begin(), idx_end());
  }

  Value *getAggregateOperand() {
    return getOperand(0);
  }
  const Value *getAggregateOperand() const {
    return getOperand(0);
  }
  static unsigned getAggregateOperandIndex() {
    return 0U;                      // get index for modifying correct operand
  }

  Value *getInsertedValueOperand() {
    return getOperand(1);
  }
  const Value *getInsertedValueOperand() const {
    return getOperand(1);
  }
  static unsigned getInsertedValueOperandIndex() {
    return 1U;                      // get index for modifying correct operand
  }

  ArrayRef<unsigned> getIndices() const {
    return Indices;
  }

  unsigned getNumIndices() const {
    return (unsigned)Indices.size();
  }

  bool hasIndices() const {
    return true;
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::InsertValue;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<InsertValueInst> :
  public FixedNumOperandTraits<InsertValueInst, 2> {
};

InsertValueInst::InsertValueInst(Value *Agg, Value *Val,
                                 ArrayRef<unsigned> Idxs, const Twine &NameStr,
                                 InsertPosition InsertBefore)
    : Instruction(Agg->getType(), InsertValue,
                  OperandTraits<InsertValueInst>::op_begin(this), 2,
                  InsertBefore) {
  init(Agg, Val, Idxs, NameStr);
}

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(InsertValueInst, Value)

//===----------------------------------------------------------------------===//
//                               PHINode Class
//===----------------------------------------------------------------------===//

// PHINode - The PHINode class is used to represent the magical mystical PHI
// node, that can not exist in nature, but can be synthesized in a computer
// scientist's overactive imagination.
//
class PHINode : public Instruction {
  /// The number of operands actually allocated.  NumOperands is
  /// the number actually in use.
  unsigned ReservedSpace;

  PHINode(const PHINode &PN);

  explicit PHINode(Type *Ty, unsigned NumReservedValues,
                   const Twine &NameStr = "",
                   InsertPosition InsertBefore = nullptr)
      : Instruction(Ty, Instruction::PHI, nullptr, 0, InsertBefore),
        ReservedSpace(NumReservedValues) {
    assert(!Ty->isTokenTy() && "PHI nodes cannot have token type!");
    setName(NameStr);
    allocHungoffUses(ReservedSpace);
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  PHINode *cloneImpl() const;

  // allocHungoffUses - this is more complicated than the generic
  // User::allocHungoffUses, because we have to allocate Uses for the incoming
  // values and pointers to the incoming blocks, all in one allocation.
  void allocHungoffUses(unsigned N) {
    User::allocHungoffUses(N, /* IsPhi */ true);
  }

public:
  /// Constructors - NumReservedValues is a hint for the number of incoming
  /// edges that this phi node will have (use 0 if you really have no idea).
  static PHINode *Create(Type *Ty, unsigned NumReservedValues,
                         const Twine &NameStr = "",
                         InsertPosition InsertBefore = nullptr) {
    return new PHINode(Ty, NumReservedValues, NameStr, InsertBefore);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Block iterator interface. This provides access to the list of incoming
  // basic blocks, which parallels the list of incoming values.
  // Please note that we are not providing non-const iterators for blocks to
  // force all updates go through an interface function.

  using block_iterator = BasicBlock **;
  using const_block_iterator = BasicBlock * const *;

  const_block_iterator block_begin() const {
    return reinterpret_cast<const_block_iterator>(op_begin() + ReservedSpace);
  }

  const_block_iterator block_end() const {
    return block_begin() + getNumOperands();
  }

  iterator_range<const_block_iterator> blocks() const {
    return make_range(block_begin(), block_end());
  }

  op_range incoming_values() { return operands(); }

  const_op_range incoming_values() const { return operands(); }

  /// Return the number of incoming edges
  ///
  unsigned getNumIncomingValues() const { return getNumOperands(); }

  /// Return incoming value number x
  ///
  Value *getIncomingValue(unsigned i) const {
    return getOperand(i);
  }
  void setIncomingValue(unsigned i, Value *V) {
    assert(V && "PHI node got a null value!");
    assert(getType() == V->getType() &&
           "All operands to PHI node must be the same type as the PHI node!");
    setOperand(i, V);
  }

  static unsigned getOperandNumForIncomingValue(unsigned i) {
    return i;
  }

  static unsigned getIncomingValueNumForOperand(unsigned i) {
    return i;
  }

  /// Return incoming basic block number @p i.
  ///
  BasicBlock *getIncomingBlock(unsigned i) const {
    return block_begin()[i];
  }

  /// Return incoming basic block corresponding
  /// to an operand of the PHI.
  ///
  BasicBlock *getIncomingBlock(const Use &U) const {
    assert(this == U.getUser() && "Iterator doesn't point to PHI's Uses?");
    return getIncomingBlock(unsigned(&U - op_begin()));
  }

  /// Return incoming basic block corresponding
  /// to value use iterator.
  ///
  BasicBlock *getIncomingBlock(Value::const_user_iterator I) const {
    return getIncomingBlock(I.getUse());
  }

  void setIncomingBlock(unsigned i, BasicBlock *BB) {
    const_cast<block_iterator>(block_begin())[i] = BB;
  }

  /// Copies the basic blocks from \p BBRange to the incoming basic block list
  /// of this PHINode, starting at \p ToIdx.
  void copyIncomingBlocks(iterator_range<const_block_iterator> BBRange,
                          uint32_t ToIdx = 0) {
    copy(BBRange, const_cast<block_iterator>(block_begin()) + ToIdx);
  }

  /// Replace every incoming basic block \p Old to basic block \p New.
  void replaceIncomingBlockWith(const BasicBlock *Old, BasicBlock *New) {
    assert(New && Old && "PHI node got a null basic block!");
    for (unsigned Op = 0, NumOps = getNumOperands(); Op != NumOps; ++Op)
      if (getIncomingBlock(Op) == Old)
        setIncomingBlock(Op, New);
  }

  /// Add an incoming value to the end of the PHI list
  ///
  void addIncoming(Value *V, BasicBlock *BB) {
    if (getNumOperands() == ReservedSpace)
      growOperands();  // Get more space!
    // Initialize some new operands.
    setNumHungOffUseOperands(getNumOperands() + 1);
    setIncomingValue(getNumOperands() - 1, V);
    setIncomingBlock(getNumOperands() - 1, BB);
  }

  /// Remove an incoming value.  This is useful if a
  /// predecessor basic block is deleted.  The value removed is returned.
  ///
  /// If the last incoming value for a PHI node is removed (and DeletePHIIfEmpty
  /// is true), the PHI node is destroyed and any uses of it are replaced with
  /// dummy values.  The only time there should be zero incoming values to a PHI
  /// node is when the block is dead, so this strategy is sound.
  ///
  Value *removeIncomingValue(unsigned Idx, bool DeletePHIIfEmpty = true);

  Value *removeIncomingValue(const BasicBlock *BB, bool DeletePHIIfEmpty=true) {
    int Idx = getBasicBlockIndex(BB);
    assert(Idx >= 0 && "Invalid basic block argument to remove!");
    return removeIncomingValue(Idx, DeletePHIIfEmpty);
  }

  /// Remove all incoming values for which the predicate returns true.
  /// The predicate accepts the incoming value index.
  void removeIncomingValueIf(function_ref<bool(unsigned)> Predicate,
                             bool DeletePHIIfEmpty = true);

  /// Return the first index of the specified basic
  /// block in the value list for this PHI.  Returns -1 if no instance.
  ///
  int getBasicBlockIndex(const BasicBlock *BB) const {
    for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
      if (block_begin()[i] == BB)
        return i;
    return -1;
  }

  Value *getIncomingValueForBlock(const BasicBlock *BB) const {
    int Idx = getBasicBlockIndex(BB);
    assert(Idx >= 0 && "Invalid basic block argument!");
    return getIncomingValue(Idx);
  }

  /// Set every incoming value(s) for block \p BB to \p V.
  void setIncomingValueForBlock(const BasicBlock *BB, Value *V) {
    assert(BB && "PHI node got a null basic block!");
    bool Found = false;
    for (unsigned Op = 0, NumOps = getNumOperands(); Op != NumOps; ++Op)
      if (getIncomingBlock(Op) == BB) {
        Found = true;
        setIncomingValue(Op, V);
      }
    (void)Found;
    assert(Found && "Invalid basic block argument to set!");
  }

  /// If the specified PHI node always merges together the
  /// same value, return the value, otherwise return null.
  Value *hasConstantValue() const;

  /// Whether the specified PHI node always merges
  /// together the same value, assuming undefs are equal to a unique
  /// non-undef value.
  bool hasConstantOrUndefValue() const;

  /// If the PHI node is complete which means all of its parent's predecessors
  /// have incoming value in this PHI, return true, otherwise return false.
  bool isComplete() const {
    return llvm::all_of(predecessors(getParent()),
                        [this](const BasicBlock *Pred) {
                          return getBasicBlockIndex(Pred) >= 0;
                        });
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::PHI;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  void growOperands();
};

template <>
struct OperandTraits<PHINode> : public HungoffOperandTraits<2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(PHINode, Value)

//===----------------------------------------------------------------------===//
//                           LandingPadInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// The landingpad instruction holds all of the information
/// necessary to generate correct exception handling. The landingpad instruction
/// cannot be moved from the top of a landing pad block, which itself is
/// accessible only from the 'unwind' edge of an invoke. This uses the
/// SubclassData field in Value to store whether or not the landingpad is a
/// cleanup.
///
class LandingPadInst : public Instruction {
  using CleanupField = BoolBitfieldElementT<0>;

  /// The number of operands actually allocated.  NumOperands is
  /// the number actually in use.
  unsigned ReservedSpace;

  LandingPadInst(const LandingPadInst &LP);

public:
  enum ClauseType { Catch, Filter };

private:
  explicit LandingPadInst(Type *RetTy, unsigned NumReservedValues,
                          const Twine &NameStr, InsertPosition InsertBefore);

  // Allocate space for exactly zero operands.
  void *operator new(size_t S) { return User::operator new(S); }

  void growOperands(unsigned Size);
  void init(unsigned NumReservedValues, const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  LandingPadInst *cloneImpl() const;

public:
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  /// Constructors - NumReservedClauses is a hint for the number of incoming
  /// clauses that this landingpad will have (use 0 if you really have no idea).
  static LandingPadInst *Create(Type *RetTy, unsigned NumReservedClauses,
                                const Twine &NameStr = "",
                                InsertPosition InsertBefore = nullptr);

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Return 'true' if this landingpad instruction is a
  /// cleanup. I.e., it should be run when unwinding even if its landing pad
  /// doesn't catch the exception.
  bool isCleanup() const { return getSubclassData<CleanupField>(); }

  /// Indicate that this landingpad instruction is a cleanup.
  void setCleanup(bool V) { setSubclassData<CleanupField>(V); }

  /// Add a catch or filter clause to the landing pad.
  void addClause(Constant *ClauseVal);

  /// Get the value of the clause at index Idx. Use isCatch/isFilter to
  /// determine what type of clause this is.
  Constant *getClause(unsigned Idx) const {
    return cast<Constant>(getOperandList()[Idx]);
  }

  /// Return 'true' if the clause and index Idx is a catch clause.
  bool isCatch(unsigned Idx) const {
    return !isa<ArrayType>(getOperandList()[Idx]->getType());
  }

  /// Return 'true' if the clause and index Idx is a filter clause.
  bool isFilter(unsigned Idx) const {
    return isa<ArrayType>(getOperandList()[Idx]->getType());
  }

  /// Get the number of clauses for this landing pad.
  unsigned getNumClauses() const { return getNumOperands(); }

  /// Grow the size of the operand list to accommodate the new
  /// number of clauses.
  void reserveClauses(unsigned Size) { growOperands(Size); }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::LandingPad;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<LandingPadInst> : public HungoffOperandTraits<1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(LandingPadInst, Value)

//===----------------------------------------------------------------------===//
//                               ReturnInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// Return a value (possibly void), from a function.  Execution
/// does not continue in this function any longer.
///
class ReturnInst : public Instruction {
  ReturnInst(const ReturnInst &RI);

private:
  // ReturnInst constructors:
  // ReturnInst()                  - 'ret void' instruction
  // ReturnInst(    null)          - 'ret void' instruction
  // ReturnInst(Value* X)          - 'ret X'    instruction
  // ReturnInst(null, Iterator It) - 'ret void' instruction, insert before I
  // ReturnInst(Value* X, Iterator It) - 'ret X'    instruction, insert before I
  // ReturnInst(    null, Inst *I) - 'ret void' instruction, insert before I
  // ReturnInst(Value* X, Inst *I) - 'ret X'    instruction, insert before I
  // ReturnInst(    null, BB *B)   - 'ret void' instruction, insert @ end of B
  // ReturnInst(Value* X, BB *B)   - 'ret X'    instruction, insert @ end of B
  //
  // NOTE: If the Value* passed is of type void then the constructor behaves as
  // if it was passed NULL.
  explicit ReturnInst(LLVMContext &C, Value *retVal = nullptr,
                      InsertPosition InsertBefore = nullptr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  ReturnInst *cloneImpl() const;

public:
  static ReturnInst *Create(LLVMContext &C, Value *retVal = nullptr,
                            InsertPosition InsertBefore = nullptr) {
    return new(!!retVal) ReturnInst(C, retVal, InsertBefore);
  }

  static ReturnInst *Create(LLVMContext &C, BasicBlock *InsertAtEnd) {
    return new (0) ReturnInst(C, nullptr, InsertAtEnd);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Convenience accessor. Returns null if there is no return value.
  Value *getReturnValue() const {
    return getNumOperands() != 0 ? getOperand(0) : nullptr;
  }

  unsigned getNumSuccessors() const { return 0; }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::Ret);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  BasicBlock *getSuccessor(unsigned idx) const {
    llvm_unreachable("ReturnInst has no successors!");
  }

  void setSuccessor(unsigned idx, BasicBlock *B) {
    llvm_unreachable("ReturnInst has no successors!");
  }
};

template <>
struct OperandTraits<ReturnInst> : public VariadicOperandTraits<ReturnInst> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ReturnInst, Value)

//===----------------------------------------------------------------------===//
//                               BranchInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// Conditional or Unconditional Branch instruction.
///
class BranchInst : public Instruction {
  /// Ops list - Branches are strange.  The operands are ordered:
  ///  [Cond, FalseDest,] TrueDest.  This makes some accessors faster because
  /// they don't have to check for cond/uncond branchness. These are mostly
  /// accessed relative from op_end().
  BranchInst(const BranchInst &BI);
  // BranchInst constructors (where {B, T, F} are blocks, and C is a condition):
  // BranchInst(BB *B)                           - 'br B'
  // BranchInst(BB* T, BB *F, Value *C)          - 'br C, T, F'
  // BranchInst(BB* B, Iter It)                  - 'br B'        insert before I
  // BranchInst(BB* T, BB *F, Value *C, Iter It) - 'br C, T, F', insert before I
  // BranchInst(BB* B, Inst *I)                  - 'br B'        insert before I
  // BranchInst(BB* T, BB *F, Value *C, Inst *I) - 'br C, T, F', insert before I
  // BranchInst(BB* B, BB *I)                    - 'br B'        insert at end
  // BranchInst(BB* T, BB *F, Value *C, BB *I)   - 'br C, T, F', insert at end
  explicit BranchInst(BasicBlock *IfTrue,
                      InsertPosition InsertBefore = nullptr);
  BranchInst(BasicBlock *IfTrue, BasicBlock *IfFalse, Value *Cond,
             InsertPosition InsertBefore = nullptr);

  void AssertOK();

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  BranchInst *cloneImpl() const;

public:
  /// Iterator type that casts an operand to a basic block.
  ///
  /// This only makes sense because the successors are stored as adjacent
  /// operands for branch instructions.
  struct succ_op_iterator
      : iterator_adaptor_base<succ_op_iterator, value_op_iterator,
                              std::random_access_iterator_tag, BasicBlock *,
                              ptrdiff_t, BasicBlock *, BasicBlock *> {
    explicit succ_op_iterator(value_op_iterator I) : iterator_adaptor_base(I) {}

    BasicBlock *operator*() const { return cast<BasicBlock>(*I); }
    BasicBlock *operator->() const { return operator*(); }
  };

  /// The const version of `succ_op_iterator`.
  struct const_succ_op_iterator
      : iterator_adaptor_base<const_succ_op_iterator, const_value_op_iterator,
                              std::random_access_iterator_tag,
                              const BasicBlock *, ptrdiff_t, const BasicBlock *,
                              const BasicBlock *> {
    explicit const_succ_op_iterator(const_value_op_iterator I)
        : iterator_adaptor_base(I) {}

    const BasicBlock *operator*() const { return cast<BasicBlock>(*I); }
    const BasicBlock *operator->() const { return operator*(); }
  };

  static BranchInst *Create(BasicBlock *IfTrue,
                            InsertPosition InsertBefore = nullptr) {
    return new(1) BranchInst(IfTrue, InsertBefore);
  }

  static BranchInst *Create(BasicBlock *IfTrue, BasicBlock *IfFalse,
                            Value *Cond,
                            InsertPosition InsertBefore = nullptr) {
    return new(3) BranchInst(IfTrue, IfFalse, Cond, InsertBefore);
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  bool isUnconditional() const { return getNumOperands() == 1; }
  bool isConditional()   const { return getNumOperands() == 3; }

  Value *getCondition() const {
    assert(isConditional() && "Cannot get condition of an uncond branch!");
    return Op<-3>();
  }

  void setCondition(Value *V) {
    assert(isConditional() && "Cannot set condition of unconditional branch!");
    Op<-3>() = V;
  }

  unsigned getNumSuccessors() const { return 1+isConditional(); }

  BasicBlock *getSuccessor(unsigned i) const {
    assert(i < getNumSuccessors() && "Successor # out of range for Branch!");
    return cast_or_null<BasicBlock>((&Op<-1>() - i)->get());
  }

  void setSuccessor(unsigned idx, BasicBlock *NewSucc) {
    assert(idx < getNumSuccessors() && "Successor # out of range for Branch!");
    *(&Op<-1>() - idx) = NewSucc;
  }

  /// Swap the successors of this branch instruction.
  ///
  /// Swaps the successors of the branch instruction. This also swaps any
  /// branch weight metadata associated with the instruction so that it
  /// continues to map correctly to each operand.
  void swapSuccessors();

  iterator_range<succ_op_iterator> successors() {
    return make_range(
        succ_op_iterator(std::next(value_op_begin(), isConditional() ? 1 : 0)),
        succ_op_iterator(value_op_end()));
  }

  iterator_range<const_succ_op_iterator> successors() const {
    return make_range(const_succ_op_iterator(
                          std::next(value_op_begin(), isConditional() ? 1 : 0)),
                      const_succ_op_iterator(value_op_end()));
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::Br);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<BranchInst> : public VariadicOperandTraits<BranchInst, 1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(BranchInst, Value)

//===----------------------------------------------------------------------===//
//                               SwitchInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// Multiway switch
///
class SwitchInst : public Instruction {
  unsigned ReservedSpace;

  // Operand[0]    = Value to switch on
  // Operand[1]    = Default basic block destination
  // Operand[2n  ] = Value to match
  // Operand[2n+1] = BasicBlock to go to on match
  SwitchInst(const SwitchInst &SI);

  /// Create a new switch instruction, specifying a value to switch on and a
  /// default destination. The number of additional cases can be specified here
  /// to make memory allocation more efficient. This constructor can also
  /// auto-insert before another instruction.
  SwitchInst(Value *Value, BasicBlock *Default, unsigned NumCases,
             InsertPosition InsertBefore);

  // allocate space for exactly zero operands
  void *operator new(size_t S) { return User::operator new(S); }

  void init(Value *Value, BasicBlock *Default, unsigned NumReserved);
  void growOperands();

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  SwitchInst *cloneImpl() const;

public:
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  // -2
  static const unsigned DefaultPseudoIndex = static_cast<unsigned>(~0L-1);

  template <typename CaseHandleT> class CaseIteratorImpl;

  /// A handle to a particular switch case. It exposes a convenient interface
  /// to both the case value and the successor block.
  ///
  /// We define this as a template and instantiate it to form both a const and
  /// non-const handle.
  template <typename SwitchInstT, typename ConstantIntT, typename BasicBlockT>
  class CaseHandleImpl {
    // Directly befriend both const and non-const iterators.
    friend class SwitchInst::CaseIteratorImpl<
        CaseHandleImpl<SwitchInstT, ConstantIntT, BasicBlockT>>;

  protected:
    // Expose the switch type we're parameterized with to the iterator.
    using SwitchInstType = SwitchInstT;

    SwitchInstT *SI;
    ptrdiff_t Index;

    CaseHandleImpl() = default;
    CaseHandleImpl(SwitchInstT *SI, ptrdiff_t Index) : SI(SI), Index(Index) {}

  public:
    /// Resolves case value for current case.
    ConstantIntT *getCaseValue() const {
      assert((unsigned)Index < SI->getNumCases() &&
             "Index out the number of cases.");
      return reinterpret_cast<ConstantIntT *>(SI->getOperand(2 + Index * 2));
    }

    /// Resolves successor for current case.
    BasicBlockT *getCaseSuccessor() const {
      assert(((unsigned)Index < SI->getNumCases() ||
              (unsigned)Index == DefaultPseudoIndex) &&
             "Index out the number of cases.");
      return SI->getSuccessor(getSuccessorIndex());
    }

    /// Returns number of current case.
    unsigned getCaseIndex() const { return Index; }

    /// Returns successor index for current case successor.
    unsigned getSuccessorIndex() const {
      assert(((unsigned)Index == DefaultPseudoIndex ||
              (unsigned)Index < SI->getNumCases()) &&
             "Index out the number of cases.");
      return (unsigned)Index != DefaultPseudoIndex ? Index + 1 : 0;
    }

    bool operator==(const CaseHandleImpl &RHS) const {
      assert(SI == RHS.SI && "Incompatible operators.");
      return Index == RHS.Index;
    }
  };

  using ConstCaseHandle =
      CaseHandleImpl<const SwitchInst, const ConstantInt, const BasicBlock>;

  class CaseHandle
      : public CaseHandleImpl<SwitchInst, ConstantInt, BasicBlock> {
    friend class SwitchInst::CaseIteratorImpl<CaseHandle>;

  public:
    CaseHandle(SwitchInst *SI, ptrdiff_t Index) : CaseHandleImpl(SI, Index) {}

    /// Sets the new value for current case.
    void setValue(ConstantInt *V) const {
      assert((unsigned)Index < SI->getNumCases() &&
             "Index out the number of cases.");
      SI->setOperand(2 + Index*2, reinterpret_cast<Value*>(V));
    }

    /// Sets the new successor for current case.
    void setSuccessor(BasicBlock *S) const {
      SI->setSuccessor(getSuccessorIndex(), S);
    }
  };

  template <typename CaseHandleT>
  class CaseIteratorImpl
      : public iterator_facade_base<CaseIteratorImpl<CaseHandleT>,
                                    std::random_access_iterator_tag,
                                    const CaseHandleT> {
    using SwitchInstT = typename CaseHandleT::SwitchInstType;

    CaseHandleT Case;

  public:
    /// Default constructed iterator is in an invalid state until assigned to
    /// a case for a particular switch.
    CaseIteratorImpl() = default;

    /// Initializes case iterator for given SwitchInst and for given
    /// case number.
    CaseIteratorImpl(SwitchInstT *SI, unsigned CaseNum) : Case(SI, CaseNum) {}

    /// Initializes case iterator for given SwitchInst and for given
    /// successor index.
    static CaseIteratorImpl fromSuccessorIndex(SwitchInstT *SI,
                                               unsigned SuccessorIndex) {
      assert(SuccessorIndex < SI->getNumSuccessors() &&
             "Successor index # out of range!");
      return SuccessorIndex != 0 ? CaseIteratorImpl(SI, SuccessorIndex - 1)
                                 : CaseIteratorImpl(SI, DefaultPseudoIndex);
    }

    /// Support converting to the const variant. This will be a no-op for const
    /// variant.
    operator CaseIteratorImpl<ConstCaseHandle>() const {
      return CaseIteratorImpl<ConstCaseHandle>(Case.SI, Case.Index);
    }

    CaseIteratorImpl &operator+=(ptrdiff_t N) {
      // Check index correctness after addition.
      // Note: Index == getNumCases() means end().
      assert(Case.Index + N >= 0 &&
             (unsigned)(Case.Index + N) <= Case.SI->getNumCases() &&
             "Case.Index out the number of cases.");
      Case.Index += N;
      return *this;
    }
    CaseIteratorImpl &operator-=(ptrdiff_t N) {
      // Check index correctness after subtraction.
      // Note: Case.Index == getNumCases() means end().
      assert(Case.Index - N >= 0 &&
             (unsigned)(Case.Index - N) <= Case.SI->getNumCases() &&
             "Case.Index out the number of cases.");
      Case.Index -= N;
      return *this;
    }
    ptrdiff_t operator-(const CaseIteratorImpl &RHS) const {
      assert(Case.SI == RHS.Case.SI && "Incompatible operators.");
      return Case.Index - RHS.Case.Index;
    }
    bool operator==(const CaseIteratorImpl &RHS) const {
      return Case == RHS.Case;
    }
    bool operator<(const CaseIteratorImpl &RHS) const {
      assert(Case.SI == RHS.Case.SI && "Incompatible operators.");
      return Case.Index < RHS.Case.Index;
    }
    const CaseHandleT &operator*() const { return Case; }
  };

  using CaseIt = CaseIteratorImpl<CaseHandle>;
  using ConstCaseIt = CaseIteratorImpl<ConstCaseHandle>;

  static SwitchInst *Create(Value *Value, BasicBlock *Default,
                            unsigned NumCases,
                            InsertPosition InsertBefore = nullptr) {
    return new SwitchInst(Value, Default, NumCases, InsertBefore);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Accessor Methods for Switch stmt
  Value *getCondition() const { return getOperand(0); }
  void setCondition(Value *V) { setOperand(0, V); }

  BasicBlock *getDefaultDest() const {
    return cast<BasicBlock>(getOperand(1));
  }

  /// Returns true if the default branch must result in immediate undefined
  /// behavior, false otherwise.
  bool defaultDestUndefined() const {
    return isa<UnreachableInst>(getDefaultDest()->getFirstNonPHIOrDbg());
  }

  void setDefaultDest(BasicBlock *DefaultCase) {
    setOperand(1, reinterpret_cast<Value*>(DefaultCase));
  }

  /// Return the number of 'cases' in this switch instruction, excluding the
  /// default case.
  unsigned getNumCases() const {
    return getNumOperands()/2 - 1;
  }

  /// Returns a read/write iterator that points to the first case in the
  /// SwitchInst.
  CaseIt case_begin() {
    return CaseIt(this, 0);
  }

  /// Returns a read-only iterator that points to the first case in the
  /// SwitchInst.
  ConstCaseIt case_begin() const {
    return ConstCaseIt(this, 0);
  }

  /// Returns a read/write iterator that points one past the last in the
  /// SwitchInst.
  CaseIt case_end() {
    return CaseIt(this, getNumCases());
  }

  /// Returns a read-only iterator that points one past the last in the
  /// SwitchInst.
  ConstCaseIt case_end() const {
    return ConstCaseIt(this, getNumCases());
  }

  /// Iteration adapter for range-for loops.
  iterator_range<CaseIt> cases() {
    return make_range(case_begin(), case_end());
  }

  /// Constant iteration adapter for range-for loops.
  iterator_range<ConstCaseIt> cases() const {
    return make_range(case_begin(), case_end());
  }

  /// Returns an iterator that points to the default case.
  /// Note: this iterator allows to resolve successor only. Attempt
  /// to resolve case value causes an assertion.
  /// Also note, that increment and decrement also causes an assertion and
  /// makes iterator invalid.
  CaseIt case_default() {
    return CaseIt(this, DefaultPseudoIndex);
  }
  ConstCaseIt case_default() const {
    return ConstCaseIt(this, DefaultPseudoIndex);
  }

  /// Search all of the case values for the specified constant. If it is
  /// explicitly handled, return the case iterator of it, otherwise return
  /// default case iterator to indicate that it is handled by the default
  /// handler.
  CaseIt findCaseValue(const ConstantInt *C) {
    return CaseIt(
        this,
        const_cast<const SwitchInst *>(this)->findCaseValue(C)->getCaseIndex());
  }
  ConstCaseIt findCaseValue(const ConstantInt *C) const {
    ConstCaseIt I = llvm::find_if(cases(), [C](const ConstCaseHandle &Case) {
      return Case.getCaseValue() == C;
    });
    if (I != case_end())
      return I;

    return case_default();
  }

  /// Finds the unique case value for a given successor. Returns null if the
  /// successor is not found, not unique, or is the default case.
  ConstantInt *findCaseDest(BasicBlock *BB) {
    if (BB == getDefaultDest())
      return nullptr;

    ConstantInt *CI = nullptr;
    for (auto Case : cases()) {
      if (Case.getCaseSuccessor() != BB)
        continue;

      if (CI)
        return nullptr; // Multiple cases lead to BB.

      CI = Case.getCaseValue();
    }

    return CI;
  }

  /// Add an entry to the switch instruction.
  /// Note:
  /// This action invalidates case_end(). Old case_end() iterator will
  /// point to the added case.
  void addCase(ConstantInt *OnVal, BasicBlock *Dest);

  /// This method removes the specified case and its successor from the switch
  /// instruction. Note that this operation may reorder the remaining cases at
  /// index idx and above.
  /// Note:
  /// This action invalidates iterators for all cases following the one removed,
  /// including the case_end() iterator. It returns an iterator for the next
  /// case.
  CaseIt removeCase(CaseIt I);

  unsigned getNumSuccessors() const { return getNumOperands()/2; }
  BasicBlock *getSuccessor(unsigned idx) const {
    assert(idx < getNumSuccessors() &&"Successor idx out of range for switch!");
    return cast<BasicBlock>(getOperand(idx*2+1));
  }
  void setSuccessor(unsigned idx, BasicBlock *NewSucc) {
    assert(idx < getNumSuccessors() && "Successor # out of range for switch!");
    setOperand(idx * 2 + 1, NewSucc);
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Switch;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

/// A wrapper class to simplify modification of SwitchInst cases along with
/// their prof branch_weights metadata.
class SwitchInstProfUpdateWrapper {
  SwitchInst &SI;
  std::optional<SmallVector<uint32_t, 8>> Weights;
  bool Changed = false;

protected:
  MDNode *buildProfBranchWeightsMD();

  void init();

public:
  using CaseWeightOpt = std::optional<uint32_t>;
  SwitchInst *operator->() { return &SI; }
  SwitchInst &operator*() { return SI; }
  operator SwitchInst *() { return &SI; }

  SwitchInstProfUpdateWrapper(SwitchInst &SI) : SI(SI) { init(); }

  ~SwitchInstProfUpdateWrapper() {
    if (Changed)
      SI.setMetadata(LLVMContext::MD_prof, buildProfBranchWeightsMD());
  }

  /// Delegate the call to the underlying SwitchInst::removeCase() and remove
  /// correspondent branch weight.
  SwitchInst::CaseIt removeCase(SwitchInst::CaseIt I);

  /// Delegate the call to the underlying SwitchInst::addCase() and set the
  /// specified branch weight for the added case.
  void addCase(ConstantInt *OnVal, BasicBlock *Dest, CaseWeightOpt W);

  /// Delegate the call to the underlying SwitchInst::eraseFromParent() and mark
  /// this object to not touch the underlying SwitchInst in destructor.
  Instruction::InstListType::iterator eraseFromParent();

  void setSuccessorWeight(unsigned idx, CaseWeightOpt W);
  CaseWeightOpt getSuccessorWeight(unsigned idx);

  static CaseWeightOpt getSuccessorWeight(const SwitchInst &SI, unsigned idx);
};

template <>
struct OperandTraits<SwitchInst> : public HungoffOperandTraits<2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(SwitchInst, Value)

//===----------------------------------------------------------------------===//
//                             IndirectBrInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// Indirect Branch Instruction.
///
class IndirectBrInst : public Instruction {
  unsigned ReservedSpace;

  // Operand[0]   = Address to jump to
  // Operand[n+1] = n-th destination
  IndirectBrInst(const IndirectBrInst &IBI);

  /// Create a new indirectbr instruction, specifying an
  /// Address to jump to.  The number of expected destinations can be specified
  /// here to make memory allocation more efficient.  This constructor can also
  /// autoinsert before another instruction.
  IndirectBrInst(Value *Address, unsigned NumDests,
                 InsertPosition InsertBefore);

  // allocate space for exactly zero operands
  void *operator new(size_t S) { return User::operator new(S); }

  void init(Value *Address, unsigned NumDests);
  void growOperands();

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  IndirectBrInst *cloneImpl() const;

public:
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  /// Iterator type that casts an operand to a basic block.
  ///
  /// This only makes sense because the successors are stored as adjacent
  /// operands for indirectbr instructions.
  struct succ_op_iterator
      : iterator_adaptor_base<succ_op_iterator, value_op_iterator,
                              std::random_access_iterator_tag, BasicBlock *,
                              ptrdiff_t, BasicBlock *, BasicBlock *> {
    explicit succ_op_iterator(value_op_iterator I) : iterator_adaptor_base(I) {}

    BasicBlock *operator*() const { return cast<BasicBlock>(*I); }
    BasicBlock *operator->() const { return operator*(); }
  };

  /// The const version of `succ_op_iterator`.
  struct const_succ_op_iterator
      : iterator_adaptor_base<const_succ_op_iterator, const_value_op_iterator,
                              std::random_access_iterator_tag,
                              const BasicBlock *, ptrdiff_t, const BasicBlock *,
                              const BasicBlock *> {
    explicit const_succ_op_iterator(const_value_op_iterator I)
        : iterator_adaptor_base(I) {}

    const BasicBlock *operator*() const { return cast<BasicBlock>(*I); }
    const BasicBlock *operator->() const { return operator*(); }
  };

  static IndirectBrInst *Create(Value *Address, unsigned NumDests,
                                InsertPosition InsertBefore = nullptr) {
    return new IndirectBrInst(Address, NumDests, InsertBefore);
  }

  /// Provide fast operand accessors.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Accessor Methods for IndirectBrInst instruction.
  Value *getAddress() { return getOperand(0); }
  const Value *getAddress() const { return getOperand(0); }
  void setAddress(Value *V) { setOperand(0, V); }

  /// return the number of possible destinations in this
  /// indirectbr instruction.
  unsigned getNumDestinations() const { return getNumOperands()-1; }

  /// Return the specified destination.
  BasicBlock *getDestination(unsigned i) { return getSuccessor(i); }
  const BasicBlock *getDestination(unsigned i) const { return getSuccessor(i); }

  /// Add a destination.
  ///
  void addDestination(BasicBlock *Dest);

  /// This method removes the specified successor from the
  /// indirectbr instruction.
  void removeDestination(unsigned i);

  unsigned getNumSuccessors() const { return getNumOperands()-1; }
  BasicBlock *getSuccessor(unsigned i) const {
    return cast<BasicBlock>(getOperand(i+1));
  }
  void setSuccessor(unsigned i, BasicBlock *NewSucc) {
    setOperand(i + 1, NewSucc);
  }

  iterator_range<succ_op_iterator> successors() {
    return make_range(succ_op_iterator(std::next(value_op_begin())),
                      succ_op_iterator(value_op_end()));
  }

  iterator_range<const_succ_op_iterator> successors() const {
    return make_range(const_succ_op_iterator(std::next(value_op_begin())),
                      const_succ_op_iterator(value_op_end()));
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::IndirectBr;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<IndirectBrInst> : public HungoffOperandTraits<1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(IndirectBrInst, Value)

//===----------------------------------------------------------------------===//
//                               InvokeInst Class
//===----------------------------------------------------------------------===//

/// Invoke instruction.  The SubclassData field is used to hold the
/// calling convention of the call.
///
class InvokeInst : public CallBase {
  /// The number of operands for this call beyond the called function,
  /// arguments, and operand bundles.
  static constexpr int NumExtraOperands = 2;

  /// The index from the end of the operand array to the normal destination.
  static constexpr int NormalDestOpEndIdx = -3;

  /// The index from the end of the operand array to the unwind destination.
  static constexpr int UnwindDestOpEndIdx = -2;

  InvokeInst(const InvokeInst &BI);

  /// Construct an InvokeInst given a range of arguments.
  ///
  /// Construct an InvokeInst from a range of arguments
  inline InvokeInst(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                    BasicBlock *IfException, ArrayRef<Value *> Args,
                    ArrayRef<OperandBundleDef> Bundles, int NumOperands,
                    const Twine &NameStr, InsertPosition InsertBefore);

  void init(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
            BasicBlock *IfException, ArrayRef<Value *> Args,
            ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr);

  /// Compute the number of operands to allocate.
  static int ComputeNumOperands(int NumArgs, int NumBundleInputs = 0) {
    // We need one operand for the called function, plus our extra operands and
    // the input operand counts provided.
    return 1 + NumExtraOperands + NumArgs + NumBundleInputs;
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  InvokeInst *cloneImpl() const;

public:
  static InvokeInst *Create(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            const Twine &NameStr,
                            InsertPosition InsertBefore = nullptr) {
    int NumOperands = ComputeNumOperands(Args.size());
    return new (NumOperands)
        InvokeInst(Ty, Func, IfNormal, IfException, Args, std::nullopt,
                   NumOperands, NameStr, InsertBefore);
  }

  static InvokeInst *Create(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            ArrayRef<OperandBundleDef> Bundles = std::nullopt,
                            const Twine &NameStr = "",
                            InsertPosition InsertBefore = nullptr) {
    int NumOperands =
        ComputeNumOperands(Args.size(), CountBundleInputs(Bundles));
    unsigned DescriptorBytes = Bundles.size() * sizeof(BundleOpInfo);

    return new (NumOperands, DescriptorBytes)
        InvokeInst(Ty, Func, IfNormal, IfException, Args, Bundles, NumOperands,
                   NameStr, InsertBefore);
  }

  static InvokeInst *Create(FunctionCallee Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            const Twine &NameStr,
                            InsertPosition InsertBefore = nullptr) {
    return Create(Func.getFunctionType(), Func.getCallee(), IfNormal,
                  IfException, Args, std::nullopt, NameStr, InsertBefore);
  }

  static InvokeInst *Create(FunctionCallee Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            ArrayRef<OperandBundleDef> Bundles = std::nullopt,
                            const Twine &NameStr = "",
                            InsertPosition InsertBefore = nullptr) {
    return Create(Func.getFunctionType(), Func.getCallee(), IfNormal,
                  IfException, Args, Bundles, NameStr, InsertBefore);
  }

  /// Create a clone of \p II with a different set of operand bundles and
  /// insert it before \p InsertBefore.
  ///
  /// The returned invoke instruction is identical to \p II in every way except
  /// that the operand bundles for the new instruction are set to the operand
  /// bundles in \p Bundles.
  static InvokeInst *Create(InvokeInst *II, ArrayRef<OperandBundleDef> Bundles,
                            InsertPosition InsertPt = nullptr);

  // get*Dest - Return the destination basic blocks...
  BasicBlock *getNormalDest() const {
    return cast<BasicBlock>(Op<NormalDestOpEndIdx>());
  }
  BasicBlock *getUnwindDest() const {
    return cast<BasicBlock>(Op<UnwindDestOpEndIdx>());
  }
  void setNormalDest(BasicBlock *B) {
    Op<NormalDestOpEndIdx>() = reinterpret_cast<Value *>(B);
  }
  void setUnwindDest(BasicBlock *B) {
    Op<UnwindDestOpEndIdx>() = reinterpret_cast<Value *>(B);
  }

  /// Get the landingpad instruction from the landing pad
  /// block (the unwind destination).
  LandingPadInst *getLandingPadInst() const;

  BasicBlock *getSuccessor(unsigned i) const {
    assert(i < 2 && "Successor # out of range for invoke!");
    return i == 0 ? getNormalDest() : getUnwindDest();
  }

  void setSuccessor(unsigned i, BasicBlock *NewSucc) {
    assert(i < 2 && "Successor # out of range for invoke!");
    if (i == 0)
      setNormalDest(NewSucc);
    else
      setUnwindDest(NewSucc);
  }

  unsigned getNumSuccessors() const { return 2; }

  /// Updates profile metadata by scaling it by \p S / \p T.
  void updateProfWeight(uint64_t S, uint64_t T);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::Invoke);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  template <typename Bitfield>
  void setSubclassData(typename Bitfield::Type Value) {
    Instruction::setSubclassData<Bitfield>(Value);
  }
};

InvokeInst::InvokeInst(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                       BasicBlock *IfException, ArrayRef<Value *> Args,
                       ArrayRef<OperandBundleDef> Bundles, int NumOperands,
                       const Twine &NameStr, InsertPosition InsertBefore)
    : CallBase(Ty->getReturnType(), Instruction::Invoke,
               OperandTraits<CallBase>::op_end(this) - NumOperands, NumOperands,
               InsertBefore) {
  init(Ty, Func, IfNormal, IfException, Args, Bundles, NameStr);
}

//===----------------------------------------------------------------------===//
//                              CallBrInst Class
//===----------------------------------------------------------------------===//

/// CallBr instruction, tracking function calls that may not return control but
/// instead transfer it to a third location. The SubclassData field is used to
/// hold the calling convention of the call.
///
class CallBrInst : public CallBase {

  unsigned NumIndirectDests;

  CallBrInst(const CallBrInst &BI);

  /// Construct a CallBrInst given a range of arguments.
  ///
  /// Construct a CallBrInst from a range of arguments
  inline CallBrInst(FunctionType *Ty, Value *Func, BasicBlock *DefaultDest,
                    ArrayRef<BasicBlock *> IndirectDests,
                    ArrayRef<Value *> Args, ArrayRef<OperandBundleDef> Bundles,
                    int NumOperands, const Twine &NameStr,
                    InsertPosition InsertBefore);

  void init(FunctionType *FTy, Value *Func, BasicBlock *DefaultDest,
            ArrayRef<BasicBlock *> IndirectDests, ArrayRef<Value *> Args,
            ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr);

  /// Compute the number of operands to allocate.
  static int ComputeNumOperands(int NumArgs, int NumIndirectDests,
                                int NumBundleInputs = 0) {
    // We need one operand for the called function, plus our extra operands and
    // the input operand counts provided.
    return 2 + NumIndirectDests + NumArgs + NumBundleInputs;
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  CallBrInst *cloneImpl() const;

public:
  static CallBrInst *Create(FunctionType *Ty, Value *Func,
                            BasicBlock *DefaultDest,
                            ArrayRef<BasicBlock *> IndirectDests,
                            ArrayRef<Value *> Args, const Twine &NameStr,
                            InsertPosition InsertBefore = nullptr) {
    int NumOperands = ComputeNumOperands(Args.size(), IndirectDests.size());
    return new (NumOperands)
        CallBrInst(Ty, Func, DefaultDest, IndirectDests, Args, std::nullopt,
                   NumOperands, NameStr, InsertBefore);
  }

  static CallBrInst *
  Create(FunctionType *Ty, Value *Func, BasicBlock *DefaultDest,
         ArrayRef<BasicBlock *> IndirectDests, ArrayRef<Value *> Args,
         ArrayRef<OperandBundleDef> Bundles = std::nullopt,
         const Twine &NameStr = "", InsertPosition InsertBefore = nullptr) {
    int NumOperands = ComputeNumOperands(Args.size(), IndirectDests.size(),
                                         CountBundleInputs(Bundles));
    unsigned DescriptorBytes = Bundles.size() * sizeof(BundleOpInfo);

    return new (NumOperands, DescriptorBytes)
        CallBrInst(Ty, Func, DefaultDest, IndirectDests, Args, Bundles,
                   NumOperands, NameStr, InsertBefore);
  }

  static CallBrInst *Create(FunctionCallee Func, BasicBlock *DefaultDest,
                            ArrayRef<BasicBlock *> IndirectDests,
                            ArrayRef<Value *> Args, const Twine &NameStr,
                            InsertPosition InsertBefore = nullptr) {
    return Create(Func.getFunctionType(), Func.getCallee(), DefaultDest,
                  IndirectDests, Args, NameStr, InsertBefore);
  }

  static CallBrInst *Create(FunctionCallee Func, BasicBlock *DefaultDest,
                            ArrayRef<BasicBlock *> IndirectDests,
                            ArrayRef<Value *> Args,
                            ArrayRef<OperandBundleDef> Bundles = std::nullopt,
                            const Twine &NameStr = "",
                            InsertPosition InsertBefore = nullptr) {
    return Create(Func.getFunctionType(), Func.getCallee(), DefaultDest,
                  IndirectDests, Args, Bundles, NameStr, InsertBefore);
  }

  /// Create a clone of \p CBI with a different set of operand bundles and
  /// insert it before \p InsertBefore.
  ///
  /// The returned callbr instruction is identical to \p CBI in every way
  /// except that the operand bundles for the new instruction are set to the
  /// operand bundles in \p Bundles.
  static CallBrInst *Create(CallBrInst *CBI, ArrayRef<OperandBundleDef> Bundles,
                            InsertPosition InsertBefore = nullptr);

  /// Return the number of callbr indirect dest labels.
  ///
  unsigned getNumIndirectDests() const { return NumIndirectDests; }

  /// getIndirectDestLabel - Return the i-th indirect dest label.
  ///
  Value *getIndirectDestLabel(unsigned i) const {
    assert(i < getNumIndirectDests() && "Out of bounds!");
    return getOperand(i + arg_size() + getNumTotalBundleOperands() + 1);
  }

  Value *getIndirectDestLabelUse(unsigned i) const {
    assert(i < getNumIndirectDests() && "Out of bounds!");
    return getOperandUse(i + arg_size() + getNumTotalBundleOperands() + 1);
  }

  // Return the destination basic blocks...
  BasicBlock *getDefaultDest() const {
    return cast<BasicBlock>(*(&Op<-1>() - getNumIndirectDests() - 1));
  }
  BasicBlock *getIndirectDest(unsigned i) const {
    return cast_or_null<BasicBlock>(*(&Op<-1>() - getNumIndirectDests() + i));
  }
  SmallVector<BasicBlock *, 16> getIndirectDests() const {
    SmallVector<BasicBlock *, 16> IndirectDests;
    for (unsigned i = 0, e = getNumIndirectDests(); i < e; ++i)
      IndirectDests.push_back(getIndirectDest(i));
    return IndirectDests;
  }
  void setDefaultDest(BasicBlock *B) {
    *(&Op<-1>() - getNumIndirectDests() - 1) = reinterpret_cast<Value *>(B);
  }
  void setIndirectDest(unsigned i, BasicBlock *B) {
    *(&Op<-1>() - getNumIndirectDests() + i) = reinterpret_cast<Value *>(B);
  }

  BasicBlock *getSuccessor(unsigned i) const {
    assert(i < getNumSuccessors() + 1 &&
           "Successor # out of range for callbr!");
    return i == 0 ? getDefaultDest() : getIndirectDest(i - 1);
  }

  void setSuccessor(unsigned i, BasicBlock *NewSucc) {
    assert(i < getNumIndirectDests() + 1 &&
           "Successor # out of range for callbr!");
    return i == 0 ? setDefaultDest(NewSucc) : setIndirectDest(i - 1, NewSucc);
  }

  unsigned getNumSuccessors() const { return getNumIndirectDests() + 1; }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::CallBr);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  template <typename Bitfield>
  void setSubclassData(typename Bitfield::Type Value) {
    Instruction::setSubclassData<Bitfield>(Value);
  }
};

CallBrInst::CallBrInst(FunctionType *Ty, Value *Func, BasicBlock *DefaultDest,
                       ArrayRef<BasicBlock *> IndirectDests,
                       ArrayRef<Value *> Args,
                       ArrayRef<OperandBundleDef> Bundles, int NumOperands,
                       const Twine &NameStr, InsertPosition InsertBefore)
    : CallBase(Ty->getReturnType(), Instruction::CallBr,
               OperandTraits<CallBase>::op_end(this) - NumOperands, NumOperands,
               InsertBefore) {
  init(Ty, Func, DefaultDest, IndirectDests, Args, Bundles, NameStr);
}

//===----------------------------------------------------------------------===//
//                              ResumeInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// Resume the propagation of an exception.
///
class ResumeInst : public Instruction {
  ResumeInst(const ResumeInst &RI);

  explicit ResumeInst(Value *Exn, InsertPosition InsertBefore = nullptr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  ResumeInst *cloneImpl() const;

public:
  static ResumeInst *Create(Value *Exn, InsertPosition InsertBefore = nullptr) {
    return new(1) ResumeInst(Exn, InsertBefore);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Convenience accessor.
  Value *getValue() const { return Op<0>(); }

  unsigned getNumSuccessors() const { return 0; }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Resume;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  BasicBlock *getSuccessor(unsigned idx) const {
    llvm_unreachable("ResumeInst has no successors!");
  }

  void setSuccessor(unsigned idx, BasicBlock *NewSucc) {
    llvm_unreachable("ResumeInst has no successors!");
  }
};

template <>
struct OperandTraits<ResumeInst> :
    public FixedNumOperandTraits<ResumeInst, 1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ResumeInst, Value)

//===----------------------------------------------------------------------===//
//                         CatchSwitchInst Class
//===----------------------------------------------------------------------===//
class CatchSwitchInst : public Instruction {
  using UnwindDestField = BoolBitfieldElementT<0>;

  /// The number of operands actually allocated.  NumOperands is
  /// the number actually in use.
  unsigned ReservedSpace;

  // Operand[0] = Outer scope
  // Operand[1] = Unwind block destination
  // Operand[n] = BasicBlock to go to on match
  CatchSwitchInst(const CatchSwitchInst &CSI);

  /// Create a new switch instruction, specifying a
  /// default destination.  The number of additional handlers can be specified
  /// here to make memory allocation more efficient.
  /// This constructor can also autoinsert before another instruction.
  CatchSwitchInst(Value *ParentPad, BasicBlock *UnwindDest,
                  unsigned NumHandlers, const Twine &NameStr,
                  InsertPosition InsertBefore);

  // allocate space for exactly zero operands
  void *operator new(size_t S) { return User::operator new(S); }

  void init(Value *ParentPad, BasicBlock *UnwindDest, unsigned NumReserved);
  void growOperands(unsigned Size);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  CatchSwitchInst *cloneImpl() const;

public:
  void operator delete(void *Ptr) { return User::operator delete(Ptr); }

  static CatchSwitchInst *Create(Value *ParentPad, BasicBlock *UnwindDest,
                                 unsigned NumHandlers,
                                 const Twine &NameStr = "",
                                 InsertPosition InsertBefore = nullptr) {
    return new CatchSwitchInst(ParentPad, UnwindDest, NumHandlers, NameStr,
                               InsertBefore);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Accessor Methods for CatchSwitch stmt
  Value *getParentPad() const { return getOperand(0); }
  void setParentPad(Value *ParentPad) { setOperand(0, ParentPad); }

  // Accessor Methods for CatchSwitch stmt
  bool hasUnwindDest() const { return getSubclassData<UnwindDestField>(); }
  bool unwindsToCaller() const { return !hasUnwindDest(); }
  BasicBlock *getUnwindDest() const {
    if (hasUnwindDest())
      return cast<BasicBlock>(getOperand(1));
    return nullptr;
  }
  void setUnwindDest(BasicBlock *UnwindDest) {
    assert(UnwindDest);
    assert(hasUnwindDest());
    setOperand(1, UnwindDest);
  }

  /// return the number of 'handlers' in this catchswitch
  /// instruction, except the default handler
  unsigned getNumHandlers() const {
    if (hasUnwindDest())
      return getNumOperands() - 2;
    return getNumOperands() - 1;
  }

private:
  static BasicBlock *handler_helper(Value *V) { return cast<BasicBlock>(V); }
  static const BasicBlock *handler_helper(const Value *V) {
    return cast<BasicBlock>(V);
  }

public:
  using DerefFnTy = BasicBlock *(*)(Value *);
  using handler_iterator = mapped_iterator<op_iterator, DerefFnTy>;
  using handler_range = iterator_range<handler_iterator>;
  using ConstDerefFnTy = const BasicBlock *(*)(const Value *);
  using const_handler_iterator =
      mapped_iterator<const_op_iterator, ConstDerefFnTy>;
  using const_handler_range = iterator_range<const_handler_iterator>;

  /// Returns an iterator that points to the first handler in CatchSwitchInst.
  handler_iterator handler_begin() {
    op_iterator It = op_begin() + 1;
    if (hasUnwindDest())
      ++It;
    return handler_iterator(It, DerefFnTy(handler_helper));
  }

  /// Returns an iterator that points to the first handler in the
  /// CatchSwitchInst.
  const_handler_iterator handler_begin() const {
    const_op_iterator It = op_begin() + 1;
    if (hasUnwindDest())
      ++It;
    return const_handler_iterator(It, ConstDerefFnTy(handler_helper));
  }

  /// Returns a read-only iterator that points one past the last
  /// handler in the CatchSwitchInst.
  handler_iterator handler_end() {
    return handler_iterator(op_end(), DerefFnTy(handler_helper));
  }

  /// Returns an iterator that points one past the last handler in the
  /// CatchSwitchInst.
  const_handler_iterator handler_end() const {
    return const_handler_iterator(op_end(), ConstDerefFnTy(handler_helper));
  }

  /// iteration adapter for range-for loops.
  handler_range handlers() {
    return make_range(handler_begin(), handler_end());
  }

  /// iteration adapter for range-for loops.
  const_handler_range handlers() const {
    return make_range(handler_begin(), handler_end());
  }

  /// Add an entry to the switch instruction...
  /// Note:
  /// This action invalidates handler_end(). Old handler_end() iterator will
  /// point to the added handler.
  void addHandler(BasicBlock *Dest);

  void removeHandler(handler_iterator HI);

  unsigned getNumSuccessors() const { return getNumOperands() - 1; }
  BasicBlock *getSuccessor(unsigned Idx) const {
    assert(Idx < getNumSuccessors() &&
           "Successor # out of range for catchswitch!");
    return cast<BasicBlock>(getOperand(Idx + 1));
  }
  void setSuccessor(unsigned Idx, BasicBlock *NewSucc) {
    assert(Idx < getNumSuccessors() &&
           "Successor # out of range for catchswitch!");
    setOperand(Idx + 1, NewSucc);
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::CatchSwitch;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<CatchSwitchInst> : public HungoffOperandTraits<2> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CatchSwitchInst, Value)

//===----------------------------------------------------------------------===//
//                               CleanupPadInst Class
//===----------------------------------------------------------------------===//
class CleanupPadInst : public FuncletPadInst {
private:
  explicit CleanupPadInst(Value *ParentPad, ArrayRef<Value *> Args,
                          unsigned Values, const Twine &NameStr,
                          InsertPosition InsertBefore)
      : FuncletPadInst(Instruction::CleanupPad, ParentPad, Args, Values,
                       NameStr, InsertBefore) {}

public:
  static CleanupPadInst *Create(Value *ParentPad,
                                ArrayRef<Value *> Args = std::nullopt,
                                const Twine &NameStr = "",
                                InsertPosition InsertBefore = nullptr) {
    unsigned Values = 1 + Args.size();
    return new (Values)
        CleanupPadInst(ParentPad, Args, Values, NameStr, InsertBefore);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::CleanupPad;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                               CatchPadInst Class
//===----------------------------------------------------------------------===//
class CatchPadInst : public FuncletPadInst {
private:
  explicit CatchPadInst(Value *CatchSwitch, ArrayRef<Value *> Args,
                        unsigned Values, const Twine &NameStr,
                        InsertPosition InsertBefore)
      : FuncletPadInst(Instruction::CatchPad, CatchSwitch, Args, Values,
                       NameStr, InsertBefore) {}

public:
  static CatchPadInst *Create(Value *CatchSwitch, ArrayRef<Value *> Args,
                              const Twine &NameStr = "",
                              InsertPosition InsertBefore = nullptr) {
    unsigned Values = 1 + Args.size();
    return new (Values)
        CatchPadInst(CatchSwitch, Args, Values, NameStr, InsertBefore);
  }

  /// Convenience accessors
  CatchSwitchInst *getCatchSwitch() const {
    return cast<CatchSwitchInst>(Op<-1>());
  }
  void setCatchSwitch(Value *CatchSwitch) {
    assert(CatchSwitch);
    Op<-1>() = CatchSwitch;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::CatchPad;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                               CatchReturnInst Class
//===----------------------------------------------------------------------===//

class CatchReturnInst : public Instruction {
  CatchReturnInst(const CatchReturnInst &RI);
  CatchReturnInst(Value *CatchPad, BasicBlock *BB, InsertPosition InsertBefore);

  void init(Value *CatchPad, BasicBlock *BB);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  CatchReturnInst *cloneImpl() const;

public:
  static CatchReturnInst *Create(Value *CatchPad, BasicBlock *BB,
                                 InsertPosition InsertBefore = nullptr) {
    assert(CatchPad);
    assert(BB);
    return new (2) CatchReturnInst(CatchPad, BB, InsertBefore);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Convenience accessors.
  CatchPadInst *getCatchPad() const { return cast<CatchPadInst>(Op<0>()); }
  void setCatchPad(CatchPadInst *CatchPad) {
    assert(CatchPad);
    Op<0>() = CatchPad;
  }

  BasicBlock *getSuccessor() const { return cast<BasicBlock>(Op<1>()); }
  void setSuccessor(BasicBlock *NewSucc) {
    assert(NewSucc);
    Op<1>() = NewSucc;
  }
  unsigned getNumSuccessors() const { return 1; }

  /// Get the parentPad of this catchret's catchpad's catchswitch.
  /// The successor block is implicitly a member of this funclet.
  Value *getCatchSwitchParentPad() const {
    return getCatchPad()->getCatchSwitch()->getParentPad();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::CatchRet);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  BasicBlock *getSuccessor(unsigned Idx) const {
    assert(Idx < getNumSuccessors() && "Successor # out of range for catchret!");
    return getSuccessor();
  }

  void setSuccessor(unsigned Idx, BasicBlock *B) {
    assert(Idx < getNumSuccessors() && "Successor # out of range for catchret!");
    setSuccessor(B);
  }
};

template <>
struct OperandTraits<CatchReturnInst>
    : public FixedNumOperandTraits<CatchReturnInst, 2> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CatchReturnInst, Value)

//===----------------------------------------------------------------------===//
//                               CleanupReturnInst Class
//===----------------------------------------------------------------------===//

class CleanupReturnInst : public Instruction {
  using UnwindDestField = BoolBitfieldElementT<0>;

private:
  CleanupReturnInst(const CleanupReturnInst &RI);
  CleanupReturnInst(Value *CleanupPad, BasicBlock *UnwindBB, unsigned Values,
                    InsertPosition InsertBefore = nullptr);

  void init(Value *CleanupPad, BasicBlock *UnwindBB);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  CleanupReturnInst *cloneImpl() const;

public:
  static CleanupReturnInst *Create(Value *CleanupPad,
                                   BasicBlock *UnwindBB = nullptr,
                                   InsertPosition InsertBefore = nullptr) {
    assert(CleanupPad);
    unsigned Values = 1;
    if (UnwindBB)
      ++Values;
    return new (Values)
        CleanupReturnInst(CleanupPad, UnwindBB, Values, InsertBefore);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  bool hasUnwindDest() const { return getSubclassData<UnwindDestField>(); }
  bool unwindsToCaller() const { return !hasUnwindDest(); }

  /// Convenience accessor.
  CleanupPadInst *getCleanupPad() const {
    return cast<CleanupPadInst>(Op<0>());
  }
  void setCleanupPad(CleanupPadInst *CleanupPad) {
    assert(CleanupPad);
    Op<0>() = CleanupPad;
  }

  unsigned getNumSuccessors() const { return hasUnwindDest() ? 1 : 0; }

  BasicBlock *getUnwindDest() const {
    return hasUnwindDest() ? cast<BasicBlock>(Op<1>()) : nullptr;
  }
  void setUnwindDest(BasicBlock *NewDest) {
    assert(NewDest);
    assert(hasUnwindDest());
    Op<1>() = NewDest;
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::CleanupRet);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  BasicBlock *getSuccessor(unsigned Idx) const {
    assert(Idx == 0);
    return getUnwindDest();
  }

  void setSuccessor(unsigned Idx, BasicBlock *B) {
    assert(Idx == 0);
    setUnwindDest(B);
  }

  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  template <typename Bitfield>
  void setSubclassData(typename Bitfield::Type Value) {
    Instruction::setSubclassData<Bitfield>(Value);
  }
};

template <>
struct OperandTraits<CleanupReturnInst>
    : public VariadicOperandTraits<CleanupReturnInst, /*MINARITY=*/1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CleanupReturnInst, Value)

//===----------------------------------------------------------------------===//
//                           UnreachableInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// This function has undefined behavior.  In particular, the
/// presence of this instruction indicates some higher level knowledge that the
/// end of the block cannot be reached.
///
class UnreachableInst : public Instruction {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  UnreachableInst *cloneImpl() const;

public:
  explicit UnreachableInst(LLVMContext &C,
                           InsertPosition InsertBefore = nullptr);

  // allocate space for exactly zero operands
  void *operator new(size_t S) { return User::operator new(S, 0); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  unsigned getNumSuccessors() const { return 0; }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Unreachable;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  BasicBlock *getSuccessor(unsigned idx) const {
    llvm_unreachable("UnreachableInst has no successors!");
  }

  void setSuccessor(unsigned idx, BasicBlock *B) {
    llvm_unreachable("UnreachableInst has no successors!");
  }
};

//===----------------------------------------------------------------------===//
//                                 TruncInst Class
//===----------------------------------------------------------------------===//

/// This class represents a truncation of integer types.
class TruncInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical TruncInst
  TruncInst *cloneImpl() const;

public:
  enum { AnyWrap = 0, NoUnsignedWrap = (1 << 0), NoSignedWrap = (1 << 1) };

  /// Constructor with insert-before-instruction semantics
  TruncInst(Value *S,                  ///< The value to be truncated
            Type *Ty,                  ///< The (smaller) type to truncate to
            const Twine &NameStr = "", ///< A name for the new instruction
            InsertPosition InsertBefore =
                nullptr ///< Where to insert the new instruction
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Trunc;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

  void setHasNoUnsignedWrap(bool B) {
    SubclassOptionalData =
        (SubclassOptionalData & ~NoUnsignedWrap) | (B * NoUnsignedWrap);
  }
  void setHasNoSignedWrap(bool B) {
    SubclassOptionalData =
        (SubclassOptionalData & ~NoSignedWrap) | (B * NoSignedWrap);
  }

  /// Test whether this operation is known to never
  /// undergo unsigned overflow, aka the nuw property.
  bool hasNoUnsignedWrap() const {
    return SubclassOptionalData & NoUnsignedWrap;
  }

  /// Test whether this operation is known to never
  /// undergo signed overflow, aka the nsw property.
  bool hasNoSignedWrap() const {
    return (SubclassOptionalData & NoSignedWrap) != 0;
  }

  /// Returns the no-wrap kind of the operation.
  unsigned getNoWrapKind() const {
    unsigned NoWrapKind = 0;
    if (hasNoUnsignedWrap())
      NoWrapKind |= NoUnsignedWrap;

    if (hasNoSignedWrap())
      NoWrapKind |= NoSignedWrap;

    return NoWrapKind;
  }
};

//===----------------------------------------------------------------------===//
//                                 ZExtInst Class
//===----------------------------------------------------------------------===//

/// This class represents zero extension of integer types.
class ZExtInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical ZExtInst
  ZExtInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  ZExtInst(Value *S,                  ///< The value to be zero extended
           Type *Ty,                  ///< The type to zero extend to
           const Twine &NameStr = "", ///< A name for the new instruction
           InsertPosition InsertBefore =
               nullptr ///< Where to insert the new instruction
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == ZExt;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 SExtInst Class
//===----------------------------------------------------------------------===//

/// This class represents a sign extension of integer types.
class SExtInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical SExtInst
  SExtInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  SExtInst(Value *S,                  ///< The value to be sign extended
           Type *Ty,                  ///< The type to sign extend to
           const Twine &NameStr = "", ///< A name for the new instruction
           InsertPosition InsertBefore =
               nullptr ///< Where to insert the new instruction
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == SExt;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 FPTruncInst Class
//===----------------------------------------------------------------------===//

/// This class represents a truncation of floating point types.
class FPTruncInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FPTruncInst
  FPTruncInst *cloneImpl() const;

public:                 /// Constructor with insert-before-instruction semantics
  FPTruncInst(Value *S, ///< The value to be truncated
              Type *Ty, ///< The type to truncate to
              const Twine &NameStr = "", ///< A name for the new instruction
              InsertPosition InsertBefore =
                  nullptr ///< Where to insert the new instruction
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == FPTrunc;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 FPExtInst Class
//===----------------------------------------------------------------------===//

/// This class represents an extension of floating point types.
class FPExtInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FPExtInst
  FPExtInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  FPExtInst(Value *S,                  ///< The value to be extended
            Type *Ty,                  ///< The type to extend to
            const Twine &NameStr = "", ///< A name for the new instruction
            InsertPosition InsertBefore =
                nullptr ///< Where to insert the new instruction
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == FPExt;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 UIToFPInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast unsigned integer to floating point.
class UIToFPInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical UIToFPInst
  UIToFPInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  UIToFPInst(Value *S,                  ///< The value to be converted
             Type *Ty,                  ///< The type to convert to
             const Twine &NameStr = "", ///< A name for the new instruction
             InsertPosition InsertBefore =
                 nullptr ///< Where to insert the new instruction
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == UIToFP;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 SIToFPInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast from signed integer to floating point.
class SIToFPInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical SIToFPInst
  SIToFPInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  SIToFPInst(Value *S,                  ///< The value to be converted
             Type *Ty,                  ///< The type to convert to
             const Twine &NameStr = "", ///< A name for the new instruction
             InsertPosition InsertBefore =
                 nullptr ///< Where to insert the new instruction
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == SIToFP;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 FPToUIInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast from floating point to unsigned integer
class FPToUIInst  : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FPToUIInst
  FPToUIInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  FPToUIInst(Value *S,                  ///< The value to be converted
             Type *Ty,                  ///< The type to convert to
             const Twine &NameStr = "", ///< A name for the new instruction
             InsertPosition InsertBefore =
                 nullptr ///< Where to insert the new instruction
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == FPToUI;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 FPToSIInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast from floating point to signed integer.
class FPToSIInst  : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FPToSIInst
  FPToSIInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  FPToSIInst(Value *S,                  ///< The value to be converted
             Type *Ty,                  ///< The type to convert to
             const Twine &NameStr = "", ///< A name for the new instruction
             InsertPosition InsertBefore =
                 nullptr ///< Where to insert the new instruction
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == FPToSI;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 IntToPtrInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast from an integer to a pointer.
class IntToPtrInst : public CastInst {
public:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Constructor with insert-before-instruction semantics
  IntToPtrInst(Value *S,                  ///< The value to be converted
               Type *Ty,                  ///< The type to convert to
               const Twine &NameStr = "", ///< A name for the new instruction
               InsertPosition InsertBefore =
                   nullptr ///< Where to insert the new instruction
  );

  /// Clone an identical IntToPtrInst.
  IntToPtrInst *cloneImpl() const;

  /// Returns the address space of this instruction's pointer type.
  unsigned getAddressSpace() const {
    return getType()->getPointerAddressSpace();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == IntToPtr;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 PtrToIntInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast from a pointer to an integer.
class PtrToIntInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical PtrToIntInst.
  PtrToIntInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  PtrToIntInst(Value *S,                  ///< The value to be converted
               Type *Ty,                  ///< The type to convert to
               const Twine &NameStr = "", ///< A name for the new instruction
               InsertPosition InsertBefore =
                   nullptr ///< Where to insert the new instruction
  );

  /// Gets the pointer operand.
  Value *getPointerOperand() { return getOperand(0); }
  /// Gets the pointer operand.
  const Value *getPointerOperand() const { return getOperand(0); }
  /// Gets the operand index of the pointer operand.
  static unsigned getPointerOperandIndex() { return 0U; }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == PtrToInt;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                             BitCastInst Class
//===----------------------------------------------------------------------===//

/// This class represents a no-op cast from one type to another.
class BitCastInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical BitCastInst.
  BitCastInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  BitCastInst(Value *S,                  ///< The value to be casted
              Type *Ty,                  ///< The type to casted to
              const Twine &NameStr = "", ///< A name for the new instruction
              InsertPosition InsertBefore =
                  nullptr ///< Where to insert the new instruction
  );

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == BitCast;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                          AddrSpaceCastInst Class
//===----------------------------------------------------------------------===//

/// This class represents a conversion between pointers from one address space
/// to another.
class AddrSpaceCastInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical AddrSpaceCastInst.
  AddrSpaceCastInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  AddrSpaceCastInst(
      Value *S,                  ///< The value to be casted
      Type *Ty,                  ///< The type to casted to
      const Twine &NameStr = "", ///< A name for the new instruction
      InsertPosition InsertBefore =
          nullptr ///< Where to insert the new instruction
  );

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == AddrSpaceCast;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

  /// Gets the pointer operand.
  Value *getPointerOperand() {
    return getOperand(0);
  }

  /// Gets the pointer operand.
  const Value *getPointerOperand() const {
    return getOperand(0);
  }

  /// Gets the operand index of the pointer operand.
  static unsigned getPointerOperandIndex() {
    return 0U;
  }

  /// Returns the address space of the pointer operand.
  unsigned getSrcAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  /// Returns the address space of the result.
  unsigned getDestAddressSpace() const {
    return getType()->getPointerAddressSpace();
  }
};

//===----------------------------------------------------------------------===//
//                          Helper functions
//===----------------------------------------------------------------------===//

/// A helper function that returns the pointer operand of a load or store
/// instruction. Returns nullptr if not load or store.
inline const Value *getLoadStorePointerOperand(const Value *V) {
  if (auto *Load = dyn_cast<LoadInst>(V))
    return Load->getPointerOperand();
  if (auto *Store = dyn_cast<StoreInst>(V))
    return Store->getPointerOperand();
  return nullptr;
}
inline Value *getLoadStorePointerOperand(Value *V) {
  return const_cast<Value *>(
      getLoadStorePointerOperand(static_cast<const Value *>(V)));
}

/// A helper function that returns the pointer operand of a load, store
/// or GEP instruction. Returns nullptr if not load, store, or GEP.
inline const Value *getPointerOperand(const Value *V) {
  if (auto *Ptr = getLoadStorePointerOperand(V))
    return Ptr;
  if (auto *Gep = dyn_cast<GetElementPtrInst>(V))
    return Gep->getPointerOperand();
  return nullptr;
}
inline Value *getPointerOperand(Value *V) {
  return const_cast<Value *>(getPointerOperand(static_cast<const Value *>(V)));
}

/// A helper function that returns the alignment of load or store instruction.
inline Align getLoadStoreAlignment(Value *I) {
  assert((isa<LoadInst>(I) || isa<StoreInst>(I)) &&
         "Expected Load or Store instruction");
  if (auto *LI = dyn_cast<LoadInst>(I))
    return LI->getAlign();
  return cast<StoreInst>(I)->getAlign();
}

/// A helper function that returns the address space of the pointer operand of
/// load or store instruction.
inline unsigned getLoadStoreAddressSpace(Value *I) {
  assert((isa<LoadInst>(I) || isa<StoreInst>(I)) &&
         "Expected Load or Store instruction");
  if (auto *LI = dyn_cast<LoadInst>(I))
    return LI->getPointerAddressSpace();
  return cast<StoreInst>(I)->getPointerAddressSpace();
}

/// A helper function that returns the type of a load or store instruction.
inline Type *getLoadStoreType(Value *I) {
  assert((isa<LoadInst>(I) || isa<StoreInst>(I)) &&
         "Expected Load or Store instruction");
  if (auto *LI = dyn_cast<LoadInst>(I))
    return LI->getType();
  return cast<StoreInst>(I)->getValueOperand()->getType();
}

/// A helper function that returns an atomic operation's sync scope; returns
/// std::nullopt if it is not an atomic operation.
inline std::optional<SyncScope::ID> getAtomicSyncScopeID(const Instruction *I) {
  if (!I->isAtomic())
    return std::nullopt;
  if (auto *AI = dyn_cast<LoadInst>(I))
    return AI->getSyncScopeID();
  if (auto *AI = dyn_cast<StoreInst>(I))
    return AI->getSyncScopeID();
  if (auto *AI = dyn_cast<FenceInst>(I))
    return AI->getSyncScopeID();
  if (auto *AI = dyn_cast<AtomicCmpXchgInst>(I))
    return AI->getSyncScopeID();
  if (auto *AI = dyn_cast<AtomicRMWInst>(I))
    return AI->getSyncScopeID();
  llvm_unreachable("unhandled atomic operation");
}

//===----------------------------------------------------------------------===//
//                              FreezeInst Class
//===----------------------------------------------------------------------===//

/// This class represents a freeze function that returns random concrete
/// value if an operand is either a poison value or an undef value
class FreezeInst : public UnaryInstruction {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FreezeInst
  FreezeInst *cloneImpl() const;

public:
  explicit FreezeInst(Value *S, const Twine &NameStr = "",
                      InsertPosition InsertBefore = nullptr);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const Instruction *I) {
    return I->getOpcode() == Freeze;
  }
  static inline bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

} // end namespace llvm

#endif // LLVM_IR_INSTRUCTIONS_H
