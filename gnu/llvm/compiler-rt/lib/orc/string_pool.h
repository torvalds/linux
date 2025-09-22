//===------- string_pool.h - Thread-safe pool for strings -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains a thread-safe string pool. Strings are ref-counted, but not
// automatically deallocated. Unused entries can be cleared by calling
// StringPool::clearDeadEntries.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_STRING_POOL_H
#define ORC_RT_STRING_POOL_H

#include <atomic>
#include <cassert>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace __orc_rt {

class PooledStringPtr;

/// String pool for strings names used by the ORC runtime.
class StringPool {
  friend class PooledStringPtr;

public:
  /// Destroy a StringPool.
  ~StringPool();

  /// Create a string pointer from the given string.
  PooledStringPtr intern(std::string S);

  /// Remove from the pool any entries that are no longer referenced.
  void clearDeadEntries();

  /// Returns true if the pool is empty.
  bool empty() const;

private:
  using RefCountType = std::atomic<size_t>;
  using PoolMap = std::unordered_map<std::string, RefCountType>;
  using PoolMapEntry = PoolMap::value_type;
  mutable std::mutex PoolMutex;
  PoolMap Pool;
};

/// Pointer to a pooled string.
class PooledStringPtr {
  friend class StringPool;
  friend struct std::hash<PooledStringPtr>;

public:
  PooledStringPtr() = default;
  PooledStringPtr(std::nullptr_t) {}
  PooledStringPtr(const PooledStringPtr &Other) : S(Other.S) {
    if (S)
      ++S->second;
  }

  PooledStringPtr &operator=(const PooledStringPtr &Other) {
    if (S) {
      assert(S->second && "Releasing PooledStringPtr with zero ref count");
      --S->second;
    }
    S = Other.S;
    if (S)
      ++S->second;
    return *this;
  }

  PooledStringPtr(PooledStringPtr &&Other) : S(nullptr) {
    std::swap(S, Other.S);
  }

  PooledStringPtr &operator=(PooledStringPtr &&Other) {
    if (S) {
      assert(S->second && "Releasing PooledStringPtr with zero ref count");
      --S->second;
    }
    S = nullptr;
    std::swap(S, Other.S);
    return *this;
  }

  ~PooledStringPtr() {
    if (S) {
      assert(S->second && "Releasing PooledStringPtr with zero ref count");
      --S->second;
    }
  }

  explicit operator bool() const { return S; }

  const std::string &operator*() const { return S->first; }

  friend bool operator==(const PooledStringPtr &LHS,
                         const PooledStringPtr &RHS) {
    return LHS.S == RHS.S;
  }

  friend bool operator!=(const PooledStringPtr &LHS,
                         const PooledStringPtr &RHS) {
    return !(LHS == RHS);
  }

  friend bool operator<(const PooledStringPtr &LHS,
                        const PooledStringPtr &RHS) {
    return LHS.S < RHS.S;
  }

private:
  using PoolEntry = StringPool::PoolMapEntry;
  using PoolEntryPtr = PoolEntry *;

  PooledStringPtr(StringPool::PoolMapEntry *S) : S(S) {
    if (S)
      ++S->second;
  }

  PoolEntryPtr S = nullptr;
};

inline StringPool::~StringPool() {
#ifndef NDEBUG
  clearDeadEntries();
  assert(Pool.empty() && "Dangling references at pool destruction time");
#endif // NDEBUG
}

inline PooledStringPtr StringPool::intern(std::string S) {
  std::lock_guard<std::mutex> Lock(PoolMutex);
  PoolMap::iterator I;
  bool Added;
  std::tie(I, Added) = Pool.try_emplace(std::move(S), 0);
  return PooledStringPtr(&*I);
}

inline void StringPool::clearDeadEntries() {
  std::lock_guard<std::mutex> Lock(PoolMutex);
  for (auto I = Pool.begin(), E = Pool.end(); I != E;) {
    auto Tmp = I++;
    if (Tmp->second == 0)
      Pool.erase(Tmp);
  }
}

inline bool StringPool::empty() const {
  std::lock_guard<std::mutex> Lock(PoolMutex);
  return Pool.empty();
}

} // end namespace __orc_rt

namespace std {

// Make PooledStringPtrs hashable.
template <> struct hash<__orc_rt::PooledStringPtr> {
  size_t operator()(const __orc_rt::PooledStringPtr &A) const {
    return hash<__orc_rt::PooledStringPtr::PoolEntryPtr>()(A.S);
  }
};

} // namespace std

#endif // ORC_RT_REF_COUNTED_STRING_POOL_H
