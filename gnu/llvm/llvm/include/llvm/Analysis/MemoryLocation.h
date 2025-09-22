//===- MemoryLocation.h - Memory location descriptions ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides utility analysis objects describing memory locations.
/// These are used both by the Alias Analysis infrastructure and more
/// specialized memory analysis layers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYLOCATION_H
#define LLVM_ANALYSIS_MEMORYLOCATION_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/TypeSize.h"

#include <optional>

namespace llvm {

class CallBase;
class Instruction;
class LoadInst;
class StoreInst;
class MemTransferInst;
class MemIntrinsic;
class AtomicCmpXchgInst;
class AtomicMemTransferInst;
class AtomicMemIntrinsic;
class AtomicRMWInst;
class AnyMemTransferInst;
class AnyMemIntrinsic;
class TargetLibraryInfo;
class VAArgInst;
class Value;

// Represents the size of a MemoryLocation. Logically, it's an
// std::optional<uint63_t> that also carries a bit to represent whether the
// integer it contains, N, is 'precise'. Precise, in this context, means that we
// know that the area of storage referenced by the given MemoryLocation must be
// precisely N bytes. An imprecise value is formed as the union of two or more
// precise values, and can conservatively represent all of the values unioned
// into it. Importantly, imprecise values are an *upper-bound* on the size of a
// MemoryLocation.
//
// Concretely, a precise MemoryLocation is (%p, 4) in
// store i32 0, i32* %p
//
// Since we know that %p must be at least 4 bytes large at this point.
// Otherwise, we have UB. An example of an imprecise MemoryLocation is (%p, 4)
// at the memcpy in
//
//   %n = select i1 %foo, i64 1, i64 4
//   call void @llvm.memcpy.p0i8.p0i8.i64(i8* %p, i8* %baz, i64 %n, i32 1,
//                                        i1 false)
//
// ...Since we'll copy *up to* 4 bytes into %p, but we can't guarantee that
// we'll ever actually do so.
//
// If asked to represent a pathologically large value, this will degrade to
// std::nullopt.
// Store Scalable information in bit 62 of Value. Scalable information is
// required to do Alias Analysis on Scalable quantities
class LocationSize {
  enum : uint64_t {
    BeforeOrAfterPointer = ~uint64_t(0),
    ScalableBit = uint64_t(1) << 62,
    AfterPointer = (BeforeOrAfterPointer - 1) & ~ScalableBit,
    MapEmpty = BeforeOrAfterPointer - 2,
    MapTombstone = BeforeOrAfterPointer - 3,
    ImpreciseBit = uint64_t(1) << 63,

    // The maximum value we can represent without falling back to 'unknown'.
    MaxValue = (MapTombstone - 1) & ~(ImpreciseBit | ScalableBit),
  };

  uint64_t Value;

  // Hack to support implicit construction. This should disappear when the
  // public LocationSize ctor goes away.
  enum DirectConstruction { Direct };

  constexpr LocationSize(uint64_t Raw, DirectConstruction) : Value(Raw) {}
  constexpr LocationSize(uint64_t Raw, bool Scalable)
      : Value(Raw > MaxValue ? AfterPointer
                             : Raw | (Scalable ? ScalableBit : uint64_t(0))) {}

  static_assert(AfterPointer & ImpreciseBit,
                "AfterPointer is imprecise by definition.");
  static_assert(BeforeOrAfterPointer & ImpreciseBit,
                "BeforeOrAfterPointer is imprecise by definition.");
  static_assert(~(MaxValue & ScalableBit), "Max value don't have bit 62 set");

public:
  // FIXME: Migrate all users to construct via either `precise` or `upperBound`,
  // to make it more obvious at the callsite the kind of size that they're
  // providing.
  //
  // Since the overwhelming majority of users of this provide precise values,
  // this assumes the provided value is precise.
  constexpr LocationSize(uint64_t Raw)
      : Value(Raw > MaxValue ? AfterPointer : Raw) {}
  // Create non-scalable LocationSize
  static LocationSize precise(uint64_t Value) {
    return LocationSize(Value, false /*Scalable*/);
  }
  static LocationSize precise(TypeSize Value) {
    return LocationSize(Value.getKnownMinValue(), Value.isScalable());
  }

  static LocationSize upperBound(uint64_t Value) {
    // You can't go lower than 0, so give a precise result.
    if (LLVM_UNLIKELY(Value == 0))
      return precise(0);
    if (LLVM_UNLIKELY(Value > MaxValue))
      return afterPointer();
    return LocationSize(Value | ImpreciseBit, Direct);
  }
  static LocationSize upperBound(TypeSize Value) {
    if (Value.isScalable())
      return afterPointer();
    return upperBound(Value.getFixedValue());
  }

  /// Any location after the base pointer (but still within the underlying
  /// object).
  constexpr static LocationSize afterPointer() {
    return LocationSize(AfterPointer, Direct);
  }

  /// Any location before or after the base pointer (but still within the
  /// underlying object).
  constexpr static LocationSize beforeOrAfterPointer() {
    return LocationSize(BeforeOrAfterPointer, Direct);
  }

  // Sentinel values, generally used for maps.
  constexpr static LocationSize mapTombstone() {
    return LocationSize(MapTombstone, Direct);
  }
  constexpr static LocationSize mapEmpty() {
    return LocationSize(MapEmpty, Direct);
  }

  // Returns a LocationSize that can correctly represent either `*this` or
  // `Other`.
  LocationSize unionWith(LocationSize Other) const {
    if (Other == *this)
      return *this;

    if (Value == BeforeOrAfterPointer || Other.Value == BeforeOrAfterPointer)
      return beforeOrAfterPointer();
    if (Value == AfterPointer || Other.Value == AfterPointer)
      return afterPointer();
    if (isScalable() || Other.isScalable())
      return afterPointer();

    return upperBound(std::max(getValue(), Other.getValue()));
  }

  bool hasValue() const {
    return Value != AfterPointer && Value != BeforeOrAfterPointer;
  }
  bool isScalable() const { return (Value & ScalableBit); }

  TypeSize getValue() const {
    assert(hasValue() && "Getting value from an unknown LocationSize!");
    assert((Value & ~(ImpreciseBit | ScalableBit)) < MaxValue &&
           "Scalable bit of value should be masked");
    return {Value & ~(ImpreciseBit | ScalableBit), isScalable()};
  }

  // Returns whether or not this value is precise. Note that if a value is
  // precise, it's guaranteed to not be unknown.
  bool isPrecise() const { return (Value & ImpreciseBit) == 0; }

  // Convenience method to check if this LocationSize's value is 0.
  bool isZero() const {
    return hasValue() && getValue().getKnownMinValue() == 0;
  }

  /// Whether accesses before the base pointer are possible.
  bool mayBeBeforePointer() const { return Value == BeforeOrAfterPointer; }

  bool operator==(const LocationSize &Other) const {
    return Value == Other.Value;
  }

  bool operator==(const TypeSize &Other) const {
    return hasValue() && getValue() == Other;
  }

  bool operator!=(const LocationSize &Other) const { return !(*this == Other); }

  bool operator!=(const TypeSize &Other) const { return !(*this == Other); }

  // Ordering operators are not provided, since it's unclear if there's only one
  // reasonable way to compare:
  // - values that don't exist against values that do, and
  // - precise values to imprecise values

  void print(raw_ostream &OS) const;

  // Returns an opaque value that represents this LocationSize. Cannot be
  // reliably converted back into a LocationSize.
  uint64_t toRaw() const { return Value; }
};

inline raw_ostream &operator<<(raw_ostream &OS, LocationSize Size) {
  Size.print(OS);
  return OS;
}

/// Representation for a specific memory location.
///
/// This abstraction can be used to represent a specific location in memory.
/// The goal of the location is to represent enough information to describe
/// abstract aliasing, modification, and reference behaviors of whatever
/// value(s) are stored in memory at the particular location.
///
/// The primary user of this interface is LLVM's Alias Analysis, but other
/// memory analyses such as MemoryDependence can use it as well.
class MemoryLocation {
public:
  /// UnknownSize - This is a special value which can be used with the
  /// size arguments in alias queries to indicate that the caller does not
  /// know the sizes of the potential memory references.
  enum : uint64_t { UnknownSize = ~UINT64_C(0) };

  /// The address of the start of the location.
  const Value *Ptr;

  /// The maximum size of the location, in address-units, or
  /// UnknownSize if the size is not known.
  ///
  /// Note that an unknown size does not mean the pointer aliases the entire
  /// virtual address space, because there are restrictions on stepping out of
  /// one object and into another. See
  /// http://llvm.org/docs/LangRef.html#pointeraliasing
  LocationSize Size;

  /// The metadata nodes which describes the aliasing of the location (each
  /// member is null if that kind of information is unavailable).
  AAMDNodes AATags;

  void print(raw_ostream &OS) const { OS << *Ptr << " " << Size << "\n"; }

  /// Return a location with information about the memory reference by the given
  /// instruction.
  static MemoryLocation get(const LoadInst *LI);
  static MemoryLocation get(const StoreInst *SI);
  static MemoryLocation get(const VAArgInst *VI);
  static MemoryLocation get(const AtomicCmpXchgInst *CXI);
  static MemoryLocation get(const AtomicRMWInst *RMWI);
  static MemoryLocation get(const Instruction *Inst) {
    return *MemoryLocation::getOrNone(Inst);
  }
  static std::optional<MemoryLocation> getOrNone(const Instruction *Inst);

  /// Return a location representing the source of a memory transfer.
  static MemoryLocation getForSource(const MemTransferInst *MTI);
  static MemoryLocation getForSource(const AtomicMemTransferInst *MTI);
  static MemoryLocation getForSource(const AnyMemTransferInst *MTI);

  /// Return a location representing the destination of a memory set or
  /// transfer.
  static MemoryLocation getForDest(const MemIntrinsic *MI);
  static MemoryLocation getForDest(const AtomicMemIntrinsic *MI);
  static MemoryLocation getForDest(const AnyMemIntrinsic *MI);
  static std::optional<MemoryLocation> getForDest(const CallBase *CI,
                                                  const TargetLibraryInfo &TLI);

  /// Return a location representing a particular argument of a call.
  static MemoryLocation getForArgument(const CallBase *Call, unsigned ArgIdx,
                                       const TargetLibraryInfo *TLI);
  static MemoryLocation getForArgument(const CallBase *Call, unsigned ArgIdx,
                                       const TargetLibraryInfo &TLI) {
    return getForArgument(Call, ArgIdx, &TLI);
  }

  /// Return a location that may access any location after Ptr, while remaining
  /// within the underlying object.
  static MemoryLocation getAfter(const Value *Ptr,
                                 const AAMDNodes &AATags = AAMDNodes()) {
    return MemoryLocation(Ptr, LocationSize::afterPointer(), AATags);
  }

  /// Return a location that may access any location before or after Ptr, while
  /// remaining within the underlying object.
  static MemoryLocation
  getBeforeOrAfter(const Value *Ptr, const AAMDNodes &AATags = AAMDNodes()) {
    return MemoryLocation(Ptr, LocationSize::beforeOrAfterPointer(), AATags);
  }

  MemoryLocation() : Ptr(nullptr), Size(LocationSize::beforeOrAfterPointer()) {}

  explicit MemoryLocation(const Value *Ptr, LocationSize Size,
                          const AAMDNodes &AATags = AAMDNodes())
      : Ptr(Ptr), Size(Size), AATags(AATags) {}

  MemoryLocation getWithNewPtr(const Value *NewPtr) const {
    MemoryLocation Copy(*this);
    Copy.Ptr = NewPtr;
    return Copy;
  }

  MemoryLocation getWithNewSize(LocationSize NewSize) const {
    MemoryLocation Copy(*this);
    Copy.Size = NewSize;
    return Copy;
  }

  MemoryLocation getWithoutAATags() const {
    MemoryLocation Copy(*this);
    Copy.AATags = AAMDNodes();
    return Copy;
  }

  bool operator==(const MemoryLocation &Other) const {
    return Ptr == Other.Ptr && Size == Other.Size && AATags == Other.AATags;
  }
};

// Specialize DenseMapInfo.
template <> struct DenseMapInfo<LocationSize> {
  static inline LocationSize getEmptyKey() { return LocationSize::mapEmpty(); }
  static inline LocationSize getTombstoneKey() {
    return LocationSize::mapTombstone();
  }
  static unsigned getHashValue(const LocationSize &Val) {
    return DenseMapInfo<uint64_t>::getHashValue(Val.toRaw());
  }
  static bool isEqual(const LocationSize &LHS, const LocationSize &RHS) {
    return LHS == RHS;
  }
};

template <> struct DenseMapInfo<MemoryLocation> {
  static inline MemoryLocation getEmptyKey() {
    return MemoryLocation(DenseMapInfo<const Value *>::getEmptyKey(),
                          DenseMapInfo<LocationSize>::getEmptyKey());
  }
  static inline MemoryLocation getTombstoneKey() {
    return MemoryLocation(DenseMapInfo<const Value *>::getTombstoneKey(),
                          DenseMapInfo<LocationSize>::getTombstoneKey());
  }
  static unsigned getHashValue(const MemoryLocation &Val) {
    return DenseMapInfo<const Value *>::getHashValue(Val.Ptr) ^
           DenseMapInfo<LocationSize>::getHashValue(Val.Size) ^
           DenseMapInfo<AAMDNodes>::getHashValue(Val.AATags);
  }
  static bool isEqual(const MemoryLocation &LHS, const MemoryLocation &RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

#endif
