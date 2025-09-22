//===- ScopedHashTable.h - A simple scoped hash table -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements an efficient scoped hash table, which is useful for
// things like dominator-based optimizations.  This allows clients to do things
// like this:
//
//  ScopedHashTable<int, int> HT;
//  {
//    ScopedHashTableScope<int, int> Scope1(HT);
//    HT.insert(0, 0);
//    HT.insert(1, 1);
//    {
//      ScopedHashTableScope<int, int> Scope2(HT);
//      HT.insert(0, 42);
//    }
//  }
//
// Looking up the value for "0" in the Scope2 block will return 42.  Looking
// up the value for 0 before 42 is inserted or after Scope2 is popped will
// return 0.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SCOPEDHASHTABLE_H
#define LLVM_ADT_SCOPEDHASHTABLE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/AllocatorBase.h"
#include <cassert>
#include <new>

namespace llvm {

template <typename K, typename V, typename KInfo = DenseMapInfo<K>,
          typename AllocatorTy = MallocAllocator>
class ScopedHashTable;

template <typename K, typename V>
class ScopedHashTableVal {
  ScopedHashTableVal *NextInScope;
  ScopedHashTableVal *NextForKey;
  K Key;
  V Val;

  ScopedHashTableVal(const K &key, const V &val) : Key(key), Val(val) {}

public:
  const K &getKey() const { return Key; }
  const V &getValue() const { return Val; }
  V &getValue() { return Val; }

  ScopedHashTableVal *getNextForKey() { return NextForKey; }
  const ScopedHashTableVal *getNextForKey() const { return NextForKey; }
  ScopedHashTableVal *getNextInScope() { return NextInScope; }

  template <typename AllocatorTy>
  static ScopedHashTableVal *Create(ScopedHashTableVal *nextInScope,
                                    ScopedHashTableVal *nextForKey,
                                    const K &key, const V &val,
                                    AllocatorTy &Allocator) {
    ScopedHashTableVal *New = Allocator.template Allocate<ScopedHashTableVal>();
    // Set up the value.
    new (New) ScopedHashTableVal(key, val);
    New->NextInScope = nextInScope;
    New->NextForKey = nextForKey;
    return New;
  }

  template <typename AllocatorTy> void Destroy(AllocatorTy &Allocator) {
    // Free memory referenced by the item.
    this->~ScopedHashTableVal();
    Allocator.Deallocate(this);
  }
};

template <typename K, typename V, typename KInfo = DenseMapInfo<K>,
          typename AllocatorTy = MallocAllocator>
class ScopedHashTableScope {
  /// HT - The hashtable that we are active for.
  ScopedHashTable<K, V, KInfo, AllocatorTy> &HT;

  /// PrevScope - This is the scope that we are shadowing in HT.
  ScopedHashTableScope *PrevScope;

  /// LastValInScope - This is the last value that was inserted for this scope
  /// or null if none have been inserted yet.
  ScopedHashTableVal<K, V> *LastValInScope;

public:
  ScopedHashTableScope(ScopedHashTable<K, V, KInfo, AllocatorTy> &HT);
  ScopedHashTableScope(ScopedHashTableScope &) = delete;
  ScopedHashTableScope &operator=(ScopedHashTableScope &) = delete;
  ~ScopedHashTableScope();

  ScopedHashTableScope *getParentScope() { return PrevScope; }
  const ScopedHashTableScope *getParentScope() const { return PrevScope; }

private:
  friend class ScopedHashTable<K, V, KInfo, AllocatorTy>;

  ScopedHashTableVal<K, V> *getLastValInScope() {
    return LastValInScope;
  }

  void setLastValInScope(ScopedHashTableVal<K, V> *Val) {
    LastValInScope = Val;
  }
};

template <typename K, typename V, typename KInfo = DenseMapInfo<K>>
class ScopedHashTableIterator {
  ScopedHashTableVal<K, V> *Node;

public:
  ScopedHashTableIterator(ScopedHashTableVal<K, V> *node) : Node(node) {}

  V &operator*() const {
    assert(Node && "Dereference end()");
    return Node->getValue();
  }
  V *operator->() const {
    return &Node->getValue();
  }

  bool operator==(const ScopedHashTableIterator &RHS) const {
    return Node == RHS.Node;
  }
  bool operator!=(const ScopedHashTableIterator &RHS) const {
    return Node != RHS.Node;
  }

  inline ScopedHashTableIterator& operator++() {          // Preincrement
    assert(Node && "incrementing past end()");
    Node = Node->getNextForKey();
    return *this;
  }
  ScopedHashTableIterator operator++(int) {        // Postincrement
    ScopedHashTableIterator tmp = *this; ++*this; return tmp;
  }
};

template <typename K, typename V, typename KInfo, typename AllocatorTy>
class ScopedHashTable : detail::AllocatorHolder<AllocatorTy> {
  using AllocTy = detail::AllocatorHolder<AllocatorTy>;

public:
  /// ScopeTy - This is a helpful typedef that allows clients to get easy access
  /// to the name of the scope for this hash table.
  using ScopeTy = ScopedHashTableScope<K, V, KInfo, AllocatorTy>;
  using size_type = unsigned;

private:
  friend class ScopedHashTableScope<K, V, KInfo, AllocatorTy>;

  using ValTy = ScopedHashTableVal<K, V>;

  DenseMap<K, ValTy*, KInfo> TopLevelMap;
  ScopeTy *CurScope = nullptr;

public:
  ScopedHashTable() = default;
  ScopedHashTable(AllocatorTy A) : AllocTy(A) {}
  ScopedHashTable(const ScopedHashTable &) = delete;
  ScopedHashTable &operator=(const ScopedHashTable &) = delete;

  ~ScopedHashTable() {
    assert(!CurScope && TopLevelMap.empty() && "Scope imbalance!");
  }

  /// Access to the allocator.
  using AllocTy::getAllocator;

  /// Return 1 if the specified key is in the table, 0 otherwise.
  size_type count(const K &Key) const {
    return TopLevelMap.count(Key);
  }

  V lookup(const K &Key) const {
    auto I = TopLevelMap.find(Key);
    if (I != TopLevelMap.end())
      return I->second->getValue();

    return V();
  }

  void insert(const K &Key, const V &Val) {
    insertIntoScope(CurScope, Key, Val);
  }

  using iterator = ScopedHashTableIterator<K, V, KInfo>;

  iterator end() { return iterator(nullptr); }

  iterator begin(const K &Key) {
    typename DenseMap<K, ValTy*, KInfo>::iterator I =
      TopLevelMap.find(Key);
    if (I == TopLevelMap.end()) return end();
    return iterator(I->second);
  }

  ScopeTy *getCurScope() { return CurScope; }
  const ScopeTy *getCurScope() const { return CurScope; }

  /// insertIntoScope - This inserts the specified key/value at the specified
  /// (possibly not the current) scope.  While it is ok to insert into a scope
  /// that isn't the current one, it isn't ok to insert *underneath* an existing
  /// value of the specified key.
  void insertIntoScope(ScopeTy *S, const K &Key, const V &Val) {
    assert(S && "No scope active!");
    ScopedHashTableVal<K, V> *&KeyEntry = TopLevelMap[Key];
    KeyEntry = ValTy::Create(S->getLastValInScope(), KeyEntry, Key, Val,
                             getAllocator());
    S->setLastValInScope(KeyEntry);
  }
};

/// ScopedHashTableScope ctor - Install this as the current scope for the hash
/// table.
template <typename K, typename V, typename KInfo, typename Allocator>
ScopedHashTableScope<K, V, KInfo, Allocator>::
  ScopedHashTableScope(ScopedHashTable<K, V, KInfo, Allocator> &ht) : HT(ht) {
  PrevScope = HT.CurScope;
  HT.CurScope = this;
  LastValInScope = nullptr;
}

template <typename K, typename V, typename KInfo, typename Allocator>
ScopedHashTableScope<K, V, KInfo, Allocator>::~ScopedHashTableScope() {
  assert(HT.CurScope == this && "Scope imbalance!");
  HT.CurScope = PrevScope;

  // Pop and delete all values corresponding to this scope.
  while (ScopedHashTableVal<K, V> *ThisEntry = LastValInScope) {
    // Pop this value out of the TopLevelMap.
    if (!ThisEntry->getNextForKey()) {
      assert(HT.TopLevelMap[ThisEntry->getKey()] == ThisEntry &&
             "Scope imbalance!");
      HT.TopLevelMap.erase(ThisEntry->getKey());
    } else {
      ScopedHashTableVal<K, V> *&KeyEntry = HT.TopLevelMap[ThisEntry->getKey()];
      assert(KeyEntry == ThisEntry && "Scope imbalance!");
      KeyEntry = ThisEntry->getNextForKey();
    }

    // Pop this value out of the scope.
    LastValInScope = ThisEntry->getNextInScope();

    // Delete this entry.
    ThisEntry->Destroy(HT.getAllocator());
  }
}

} // end namespace llvm

#endif // LLVM_ADT_SCOPEDHASHTABLE_H
