//===-- SymbolStringPool.h -- Thread-safe pool for JIT symbols --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains a thread-safe string pool suitable for use with ORC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SYMBOLSTRINGPOOL_H
#define LLVM_EXECUTIONENGINE_ORC_SYMBOLSTRINGPOOL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include <atomic>
#include <mutex>

namespace llvm {

class raw_ostream;

namespace orc {

class SymbolStringPtrBase;
class SymbolStringPtr;
class NonOwningSymbolStringPtr;

/// String pool for symbol names used by the JIT.
class SymbolStringPool {
  friend class SymbolStringPoolTest;
  friend class SymbolStringPtrBase;
  friend class SymbolStringPoolEntryUnsafe;

  // Implemented in DebugUtils.h.
  friend raw_ostream &operator<<(raw_ostream &OS, const SymbolStringPool &SSP);

public:
  /// Destroy a SymbolStringPool.
  ~SymbolStringPool();

  /// Create a symbol string pointer from the given string.
  SymbolStringPtr intern(StringRef S);

  /// Remove from the pool any entries that are no longer referenced.
  void clearDeadEntries();

  /// Returns true if the pool is empty.
  bool empty() const;

private:
  size_t getRefCount(const SymbolStringPtrBase &S) const;

  using RefCountType = std::atomic<size_t>;
  using PoolMap = StringMap<RefCountType>;
  using PoolMapEntry = StringMapEntry<RefCountType>;
  mutable std::mutex PoolMutex;
  PoolMap Pool;
};

/// Base class for both owning and non-owning symbol-string ptrs.
///
/// All symbol-string ptrs are convertible to bool, dereferenceable and
/// comparable.
///
/// SymbolStringPtrBases are default-constructible and constructible
/// from nullptr to enable comparison with these values.
class SymbolStringPtrBase {
  friend class SymbolStringPool;
  friend struct DenseMapInfo<SymbolStringPtr>;
  friend struct DenseMapInfo<NonOwningSymbolStringPtr>;

public:
  SymbolStringPtrBase() = default;
  SymbolStringPtrBase(std::nullptr_t) {}

  explicit operator bool() const { return S; }

  StringRef operator*() const { return S->first(); }

  friend bool operator==(SymbolStringPtrBase LHS, SymbolStringPtrBase RHS) {
    return LHS.S == RHS.S;
  }

  friend bool operator!=(SymbolStringPtrBase LHS, SymbolStringPtrBase RHS) {
    return !(LHS == RHS);
  }

  friend bool operator<(SymbolStringPtrBase LHS, SymbolStringPtrBase RHS) {
    return LHS.S < RHS.S;
  }

#ifndef NDEBUG
  // Returns true if the pool entry's ref count is above zero (or if the entry
  // is an empty or tombstone value). Useful for debugging and testing -- this
  // method can be used to identify SymbolStringPtrs and
  // NonOwningSymbolStringPtrs that are pointing to abandoned pool entries.
  bool poolEntryIsAlive() const {
    return isRealPoolEntry(S) ? S->getValue() != 0 : true;
  }
#endif

protected:
  using PoolEntry = SymbolStringPool::PoolMapEntry;
  using PoolEntryPtr = PoolEntry *;

  SymbolStringPtrBase(PoolEntryPtr S) : S(S) {}

  constexpr static uintptr_t EmptyBitPattern =
      std::numeric_limits<uintptr_t>::max()
      << PointerLikeTypeTraits<PoolEntryPtr>::NumLowBitsAvailable;

  constexpr static uintptr_t TombstoneBitPattern =
      (std::numeric_limits<uintptr_t>::max() - 1)
      << PointerLikeTypeTraits<PoolEntryPtr>::NumLowBitsAvailable;

  constexpr static uintptr_t InvalidPtrMask =
      (std::numeric_limits<uintptr_t>::max() - 3)
      << PointerLikeTypeTraits<PoolEntryPtr>::NumLowBitsAvailable;

  // Returns false for null, empty, and tombstone values, true otherwise.
  static bool isRealPoolEntry(PoolEntryPtr P) {
    return ((reinterpret_cast<uintptr_t>(P) - 1) & InvalidPtrMask) !=
           InvalidPtrMask;
  }

  size_t getRefCount() const {
    return isRealPoolEntry(S) ? size_t(S->getValue()) : size_t(0);
  }

  PoolEntryPtr S = nullptr;
};

/// Pointer to a pooled string representing a symbol name.
class SymbolStringPtr : public SymbolStringPtrBase {
  friend class SymbolStringPool;
  friend class SymbolStringPoolEntryUnsafe;
  friend struct DenseMapInfo<SymbolStringPtr>;

public:
  SymbolStringPtr() = default;
  SymbolStringPtr(std::nullptr_t) {}
  SymbolStringPtr(const SymbolStringPtr &Other) : SymbolStringPtrBase(Other.S) {
    incRef();
  }

  explicit SymbolStringPtr(NonOwningSymbolStringPtr Other);

  SymbolStringPtr& operator=(const SymbolStringPtr &Other) {
    decRef();
    S = Other.S;
    incRef();
    return *this;
  }

  SymbolStringPtr(SymbolStringPtr &&Other) { std::swap(S, Other.S); }

  SymbolStringPtr& operator=(SymbolStringPtr &&Other) {
    decRef();
    S = nullptr;
    std::swap(S, Other.S);
    return *this;
  }

  ~SymbolStringPtr() { decRef(); }

private:
  SymbolStringPtr(PoolEntryPtr S) : SymbolStringPtrBase(S) { incRef(); }

  void incRef() {
    if (isRealPoolEntry(S))
      ++S->getValue();
  }

  void decRef() {
    if (isRealPoolEntry(S)) {
      assert(S->getValue() && "Releasing SymbolStringPtr with zero ref count");
      --S->getValue();
    }
  }

  static SymbolStringPtr getEmptyVal() {
    return SymbolStringPtr(reinterpret_cast<PoolEntryPtr>(EmptyBitPattern));
  }

  static SymbolStringPtr getTombstoneVal() {
    return SymbolStringPtr(reinterpret_cast<PoolEntryPtr>(TombstoneBitPattern));
  }
};

/// Provides unsafe access to ownership operations on SymbolStringPtr.
/// This class can be used to manage SymbolStringPtr instances from C.
class SymbolStringPoolEntryUnsafe {
public:
  using PoolEntry = SymbolStringPool::PoolMapEntry;

  SymbolStringPoolEntryUnsafe(PoolEntry *E) : E(E) {}

  /// Create an unsafe pool entry ref without changing the ref-count.
  static SymbolStringPoolEntryUnsafe from(const SymbolStringPtr &S) {
    return S.S;
  }

  /// Consumes the given SymbolStringPtr without releasing the pool entry.
  static SymbolStringPoolEntryUnsafe take(SymbolStringPtr &&S) {
    PoolEntry *E = nullptr;
    std::swap(E, S.S);
    return E;
  }

  PoolEntry *rawPtr() { return E; }

  /// Creates a SymbolStringPtr for this entry, with the SymbolStringPtr
  /// retaining the entry as usual.
  SymbolStringPtr copyToSymbolStringPtr() { return SymbolStringPtr(E); }

  /// Creates a SymbolStringPtr for this entry *without* performing a retain
  /// operation during construction.
  SymbolStringPtr moveToSymbolStringPtr() {
    SymbolStringPtr S;
    std::swap(S.S, E);
    return S;
  }

  void retain() { ++E->getValue(); }
  void release() { --E->getValue(); }

private:
  PoolEntry *E = nullptr;
};

/// Non-owning SymbolStringPool entry pointer. Instances are comparable with
/// SymbolStringPtr instances and guaranteed to have the same hash, but do not
/// affect the ref-count of the pooled string (and are therefore cheaper to
/// copy).
///
/// NonOwningSymbolStringPtrs are silently invalidated if the pool entry's
/// ref-count drops to zero, so they should only be used in contexts where a
/// corresponding SymbolStringPtr is known to exist (which will guarantee that
/// the ref-count stays above zero). E.g. in a graph where nodes are
/// represented by SymbolStringPtrs the edges can be represented by pairs of
/// NonOwningSymbolStringPtrs and this will make the introduction of deletion
/// of edges cheaper.
class NonOwningSymbolStringPtr : public SymbolStringPtrBase {
  friend struct DenseMapInfo<orc::NonOwningSymbolStringPtr>;

public:
  NonOwningSymbolStringPtr() = default;
  explicit NonOwningSymbolStringPtr(const SymbolStringPtr &S)
      : SymbolStringPtrBase(S) {}

  using SymbolStringPtrBase::operator=;

private:
  NonOwningSymbolStringPtr(PoolEntryPtr S) : SymbolStringPtrBase(S) {}

  static NonOwningSymbolStringPtr getEmptyVal() {
    return NonOwningSymbolStringPtr(
        reinterpret_cast<PoolEntryPtr>(EmptyBitPattern));
  }

  static NonOwningSymbolStringPtr getTombstoneVal() {
    return NonOwningSymbolStringPtr(
        reinterpret_cast<PoolEntryPtr>(TombstoneBitPattern));
  }
};

inline SymbolStringPtr::SymbolStringPtr(NonOwningSymbolStringPtr Other)
    : SymbolStringPtrBase(Other) {
  assert(poolEntryIsAlive() &&
         "SymbolStringPtr constructed from invalid non-owning pointer.");

  if (isRealPoolEntry(S))
    ++S->getValue();
}

inline SymbolStringPool::~SymbolStringPool() {
#ifndef NDEBUG
  clearDeadEntries();
  assert(Pool.empty() && "Dangling references at pool destruction time");
#endif // NDEBUG
}

inline SymbolStringPtr SymbolStringPool::intern(StringRef S) {
  std::lock_guard<std::mutex> Lock(PoolMutex);
  PoolMap::iterator I;
  bool Added;
  std::tie(I, Added) = Pool.try_emplace(S, 0);
  return SymbolStringPtr(&*I);
}

inline void SymbolStringPool::clearDeadEntries() {
  std::lock_guard<std::mutex> Lock(PoolMutex);
  for (auto I = Pool.begin(), E = Pool.end(); I != E;) {
    auto Tmp = I++;
    if (Tmp->second == 0)
      Pool.erase(Tmp);
  }
}

inline bool SymbolStringPool::empty() const {
  std::lock_guard<std::mutex> Lock(PoolMutex);
  return Pool.empty();
}

inline size_t
SymbolStringPool::getRefCount(const SymbolStringPtrBase &S) const {
  return S.getRefCount();
}

} // end namespace orc

template <>
struct DenseMapInfo<orc::SymbolStringPtr> {

  static orc::SymbolStringPtr getEmptyKey() {
    return orc::SymbolStringPtr::getEmptyVal();
  }

  static orc::SymbolStringPtr getTombstoneKey() {
    return orc::SymbolStringPtr::getTombstoneVal();
  }

  static unsigned getHashValue(const orc::SymbolStringPtrBase &V) {
    return DenseMapInfo<orc::SymbolStringPtr::PoolEntryPtr>::getHashValue(V.S);
  }

  static bool isEqual(const orc::SymbolStringPtrBase &LHS,
                      const orc::SymbolStringPtrBase &RHS) {
    return LHS.S == RHS.S;
  }
};

template <> struct DenseMapInfo<orc::NonOwningSymbolStringPtr> {

  static orc::NonOwningSymbolStringPtr getEmptyKey() {
    return orc::NonOwningSymbolStringPtr::getEmptyVal();
  }

  static orc::NonOwningSymbolStringPtr getTombstoneKey() {
    return orc::NonOwningSymbolStringPtr::getTombstoneVal();
  }

  static unsigned getHashValue(const orc::SymbolStringPtrBase &V) {
    return DenseMapInfo<
        orc::NonOwningSymbolStringPtr::PoolEntryPtr>::getHashValue(V.S);
  }

  static bool isEqual(const orc::SymbolStringPtrBase &LHS,
                      const orc::SymbolStringPtrBase &RHS) {
    return LHS.S == RHS.S;
  }
};

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SYMBOLSTRINGPOOL_H
