//===--- ImmutableMap.h - Immutable (functional) map interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the ImmutableMap class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_IMMUTABLEMAP_H
#define LLVM_ADT_IMMUTABLEMAP_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/Support/Allocator.h"
#include <utility>

namespace llvm {

/// ImutKeyValueInfo -Traits class used by ImmutableMap.  While both the first
/// and second elements in a pair are used to generate profile information,
/// only the first element (the key) is used by isEqual and isLess.
template <typename T, typename S>
struct ImutKeyValueInfo {
  using value_type = const std::pair<T,S>;
  using value_type_ref = const value_type&;
  using key_type = const T;
  using key_type_ref = const T&;
  using data_type = const S;
  using data_type_ref = const S&;

  static inline key_type_ref KeyOfValue(value_type_ref V) {
    return V.first;
  }

  static inline data_type_ref DataOfValue(value_type_ref V) {
    return V.second;
  }

  static inline bool isEqual(key_type_ref L, key_type_ref R) {
    return ImutContainerInfo<T>::isEqual(L,R);
  }
  static inline bool isLess(key_type_ref L, key_type_ref R) {
    return ImutContainerInfo<T>::isLess(L,R);
  }

  static inline bool isDataEqual(data_type_ref L, data_type_ref R) {
    return ImutContainerInfo<S>::isEqual(L,R);
  }

  static inline void Profile(FoldingSetNodeID& ID, value_type_ref V) {
    ImutContainerInfo<T>::Profile(ID, V.first);
    ImutContainerInfo<S>::Profile(ID, V.second);
  }
};

template <typename KeyT, typename ValT,
          typename ValInfo = ImutKeyValueInfo<KeyT,ValT>>
class ImmutableMap {
public:
  using value_type = typename ValInfo::value_type;
  using value_type_ref = typename ValInfo::value_type_ref;
  using key_type = typename ValInfo::key_type;
  using key_type_ref = typename ValInfo::key_type_ref;
  using data_type = typename ValInfo::data_type;
  using data_type_ref = typename ValInfo::data_type_ref;
  using TreeTy = ImutAVLTree<ValInfo>;

protected:
  IntrusiveRefCntPtr<TreeTy> Root;

public:
  /// Constructs a map from a pointer to a tree root.  In general one
  /// should use a Factory object to create maps instead of directly
  /// invoking the constructor, but there are cases where make this
  /// constructor public is useful.
  explicit ImmutableMap(const TreeTy *R) : Root(const_cast<TreeTy *>(R)) {}

  class Factory {
    typename TreeTy::Factory F;
    const bool Canonicalize;

  public:
    Factory(bool canonicalize = true) : Canonicalize(canonicalize) {}

    Factory(BumpPtrAllocator &Alloc, bool canonicalize = true)
        : F(Alloc), Canonicalize(canonicalize) {}

    Factory(const Factory &) = delete;
    Factory &operator=(const Factory &) = delete;

    ImmutableMap getEmptyMap() { return ImmutableMap(F.getEmptyTree()); }

    [[nodiscard]] ImmutableMap add(ImmutableMap Old, key_type_ref K,
                                   data_type_ref D) {
      TreeTy *T = F.add(Old.Root.get(), std::pair<key_type, data_type>(K, D));
      return ImmutableMap(Canonicalize ? F.getCanonicalTree(T): T);
    }

    [[nodiscard]] ImmutableMap remove(ImmutableMap Old, key_type_ref K) {
      TreeTy *T = F.remove(Old.Root.get(), K);
      return ImmutableMap(Canonicalize ? F.getCanonicalTree(T): T);
    }

    typename TreeTy::Factory *getTreeFactory() const {
      return const_cast<typename TreeTy::Factory *>(&F);
    }
  };

  bool contains(key_type_ref K) const {
    return Root ? Root->contains(K) : false;
  }

  bool operator==(const ImmutableMap &RHS) const {
    return Root && RHS.Root ? Root->isEqual(*RHS.Root.get()) : Root == RHS.Root;
  }

  bool operator!=(const ImmutableMap &RHS) const {
    return Root && RHS.Root ? Root->isNotEqual(*RHS.Root.get())
                            : Root != RHS.Root;
  }

  TreeTy *getRoot() const {
    if (Root) { Root->retain(); }
    return Root.get();
  }

  TreeTy *getRootWithoutRetain() const { return Root.get(); }

  void manualRetain() {
    if (Root) Root->retain();
  }

  void manualRelease() {
    if (Root) Root->release();
  }

  bool isEmpty() const { return !Root; }

public:
  //===--------------------------------------------------===//
  // For testing.
  //===--------------------------------------------------===//

  void verify() const { if (Root) Root->verify(); }

  //===--------------------------------------------------===//
  // Iterators.
  //===--------------------------------------------------===//

  class iterator : public ImutAVLValueIterator<ImmutableMap> {
    friend class ImmutableMap;

    iterator() = default;
    explicit iterator(TreeTy *Tree) : iterator::ImutAVLValueIterator(Tree) {}

  public:
    key_type_ref getKey() const { return (*this)->first; }
    data_type_ref getData() const { return (*this)->second; }
  };

  iterator begin() const { return iterator(Root.get()); }
  iterator end() const { return iterator(); }

  data_type* lookup(key_type_ref K) const {
    if (Root) {
      TreeTy* T = Root->find(K);
      if (T) return &T->getValue().second;
    }

    return nullptr;
  }

  /// getMaxElement - Returns the <key,value> pair in the ImmutableMap for
  ///  which key is the highest in the ordering of keys in the map.  This
  ///  method returns NULL if the map is empty.
  value_type* getMaxElement() const {
    return Root ? &(Root->getMaxElement()->getValue()) : nullptr;
  }

  //===--------------------------------------------------===//
  // Utility methods.
  //===--------------------------------------------------===//

  unsigned getHeight() const { return Root ? Root->getHeight() : 0; }

  static inline void Profile(FoldingSetNodeID& ID, const ImmutableMap& M) {
    ID.AddPointer(M.Root.get());
  }

  inline void Profile(FoldingSetNodeID& ID) const {
    return Profile(ID,*this);
  }
};

// NOTE: This will possibly become the new implementation of ImmutableMap some day.
template <typename KeyT, typename ValT,
typename ValInfo = ImutKeyValueInfo<KeyT,ValT>>
class ImmutableMapRef {
public:
  using value_type = typename ValInfo::value_type;
  using value_type_ref = typename ValInfo::value_type_ref;
  using key_type = typename ValInfo::key_type;
  using key_type_ref = typename ValInfo::key_type_ref;
  using data_type = typename ValInfo::data_type;
  using data_type_ref = typename ValInfo::data_type_ref;
  using TreeTy = ImutAVLTree<ValInfo>;
  using FactoryTy = typename TreeTy::Factory;

protected:
  IntrusiveRefCntPtr<TreeTy> Root;
  FactoryTy *Factory;

public:
  /// Constructs a map from a pointer to a tree root.  In general one
  /// should use a Factory object to create maps instead of directly
  /// invoking the constructor, but there are cases where make this
  /// constructor public is useful.
  ImmutableMapRef(const TreeTy *R, FactoryTy *F)
      : Root(const_cast<TreeTy *>(R)), Factory(F) {}

  ImmutableMapRef(const ImmutableMap<KeyT, ValT> &X,
                  typename ImmutableMap<KeyT, ValT>::Factory &F)
      : Root(X.getRootWithoutRetain()), Factory(F.getTreeFactory()) {}

  static inline ImmutableMapRef getEmptyMap(FactoryTy *F) {
    return ImmutableMapRef(nullptr, F);
  }

  void manualRetain() {
    if (Root) Root->retain();
  }

  void manualRelease() {
    if (Root) Root->release();
  }

  ImmutableMapRef add(key_type_ref K, data_type_ref D) const {
    TreeTy *NewT =
        Factory->add(Root.get(), std::pair<key_type, data_type>(K, D));
    return ImmutableMapRef(NewT, Factory);
  }

  ImmutableMapRef remove(key_type_ref K) const {
    TreeTy *NewT = Factory->remove(Root.get(), K);
    return ImmutableMapRef(NewT, Factory);
  }

  bool contains(key_type_ref K) const {
    return Root ? Root->contains(K) : false;
  }

  ImmutableMap<KeyT, ValT> asImmutableMap() const {
    return ImmutableMap<KeyT, ValT>(Factory->getCanonicalTree(Root.get()));
  }

  bool operator==(const ImmutableMapRef &RHS) const {
    return Root && RHS.Root ? Root->isEqual(*RHS.Root.get()) : Root == RHS.Root;
  }

  bool operator!=(const ImmutableMapRef &RHS) const {
    return Root && RHS.Root ? Root->isNotEqual(*RHS.Root.get())
                            : Root != RHS.Root;
  }

  bool isEmpty() const { return !Root; }

  //===--------------------------------------------------===//
  // For testing.
  //===--------------------------------------------------===//

  void verify() const {
    if (Root)
      Root->verify();
  }

  //===--------------------------------------------------===//
  // Iterators.
  //===--------------------------------------------------===//

  class iterator : public ImutAVLValueIterator<ImmutableMapRef> {
    friend class ImmutableMapRef;

    iterator() = default;
    explicit iterator(TreeTy *Tree) : iterator::ImutAVLValueIterator(Tree) {}

  public:
    key_type_ref getKey() const { return (*this)->first; }
    data_type_ref getData() const { return (*this)->second; }
  };

  iterator begin() const { return iterator(Root.get()); }
  iterator end() const { return iterator(); }

  data_type *lookup(key_type_ref K) const {
    if (Root) {
      TreeTy* T = Root->find(K);
      if (T) return &T->getValue().second;
    }

    return nullptr;
  }

  /// getMaxElement - Returns the <key,value> pair in the ImmutableMap for
  ///  which key is the highest in the ordering of keys in the map.  This
  ///  method returns NULL if the map is empty.
  value_type* getMaxElement() const {
    return Root ? &(Root->getMaxElement()->getValue()) : nullptr;
  }

  //===--------------------------------------------------===//
  // Utility methods.
  //===--------------------------------------------------===//

  unsigned getHeight() const { return Root ? Root->getHeight() : 0; }

  static inline void Profile(FoldingSetNodeID &ID, const ImmutableMapRef &M) {
    ID.AddPointer(M.Root.get());
  }

  inline void Profile(FoldingSetNodeID &ID) const { return Profile(ID, *this); }
};

} // end namespace llvm

#endif // LLVM_ADT_IMMUTABLEMAP_H
