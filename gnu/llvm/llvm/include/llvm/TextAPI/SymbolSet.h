//===- llvm/TextAPI/SymbolSet.h - TAPI Symbol Set --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_SYMBOLSET_H
#define LLVM_TEXTAPI_SYMBOLSET_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/Symbol.h"
#include <stddef.h>

namespace llvm {

struct SymbolsMapKey {
  MachO::EncodeKind Kind;
  StringRef Name;

  SymbolsMapKey(MachO::EncodeKind Kind, StringRef Name)
      : Kind(Kind), Name(Name) {}
};
template <> struct DenseMapInfo<SymbolsMapKey> {
  static inline SymbolsMapKey getEmptyKey() {
    return SymbolsMapKey(MachO::EncodeKind::GlobalSymbol, StringRef{});
  }

  static inline SymbolsMapKey getTombstoneKey() {
    return SymbolsMapKey(MachO::EncodeKind::ObjectiveCInstanceVariable,
                         StringRef{});
  }

  static unsigned getHashValue(const SymbolsMapKey &Key) {
    return hash_combine(hash_value(Key.Kind), hash_value(Key.Name));
  }

  static bool isEqual(const SymbolsMapKey &LHS, const SymbolsMapKey &RHS) {
    return std::tie(LHS.Kind, LHS.Name) == std::tie(RHS.Kind, RHS.Name);
  }
};

template <typename DerivedT, typename KeyInfoT, typename BucketT>
bool operator==(const DenseMapBase<DerivedT, SymbolsMapKey, MachO::Symbol *,
                                   KeyInfoT, BucketT> &LHS,
                const DenseMapBase<DerivedT, SymbolsMapKey, MachO::Symbol *,
                                   KeyInfoT, BucketT> &RHS) {
  if (LHS.size() != RHS.size())
    return false;
  for (const auto &KV : LHS) {
    auto I = RHS.find(KV.first);
    if (I == RHS.end() || *I->second != *KV.second)
      return false;
  }
  return true;
}

template <typename DerivedT, typename KeyInfoT, typename BucketT>
bool operator!=(const DenseMapBase<DerivedT, SymbolsMapKey, MachO::Symbol *,
                                   KeyInfoT, BucketT> &LHS,
                const DenseMapBase<DerivedT, SymbolsMapKey, MachO::Symbol *,
                                   KeyInfoT, BucketT> &RHS) {
  return !(LHS == RHS);
}

namespace MachO {

class SymbolSet {
private:
  llvm::BumpPtrAllocator Allocator;
  StringRef copyString(StringRef String) {
    if (String.empty())
      return {};
    void *Ptr = Allocator.Allocate(String.size(), 1);
    memcpy(Ptr, String.data(), String.size());
    return StringRef(reinterpret_cast<const char *>(Ptr), String.size());
  }

  using SymbolsMapType = llvm::DenseMap<SymbolsMapKey, Symbol *>;
  SymbolsMapType Symbols;

  Symbol *addGlobalImpl(EncodeKind, StringRef Name, SymbolFlags Flags);

public:
  SymbolSet() = default;
  Symbol *addGlobal(EncodeKind Kind, StringRef Name, SymbolFlags Flags,
                    const Target &Targ);
  size_t size() const { return Symbols.size(); }

  template <typename RangeT, typename ElT = std::remove_reference_t<
                                 decltype(*std::begin(std::declval<RangeT>()))>>
  Symbol *addGlobal(EncodeKind Kind, StringRef Name, SymbolFlags Flags,
                    RangeT &&Targets) {
    auto *Global = addGlobalImpl(Kind, Name, Flags);
    for (const auto &Targ : Targets)
      Global->addTarget(Targ);
    if (Kind == EncodeKind::ObjectiveCClassEHType)
      addGlobal(EncodeKind::ObjectiveCClass, Name, Flags, Targets);
    return Global;
  }

  const Symbol *
  findSymbol(EncodeKind Kind, StringRef Name,
             ObjCIFSymbolKind ObjCIF = ObjCIFSymbolKind::None) const;

  struct const_symbol_iterator
      : public iterator_adaptor_base<
            const_symbol_iterator, SymbolsMapType::const_iterator,
            std::forward_iterator_tag, const Symbol *, ptrdiff_t,
            const Symbol *, const Symbol *> {
    const_symbol_iterator() = default;

    template <typename U>
    const_symbol_iterator(U &&u)
        : iterator_adaptor_base(std::forward<U &&>(u)) {}

    reference operator*() const { return I->second; }
    pointer operator->() const { return I->second; }
  };

  using const_symbol_range = iterator_range<const_symbol_iterator>;

  using const_filtered_symbol_iterator =
      filter_iterator<const_symbol_iterator,
                      std::function<bool(const Symbol *)>>;
  using const_filtered_symbol_range =
      iterator_range<const_filtered_symbol_iterator>;

  // Range that contains all symbols.
  const_symbol_range symbols() const {
    return {Symbols.begin(), Symbols.end()};
  }

  // Range that contains all defined and exported symbols.
  const_filtered_symbol_range exports() const {
    std::function<bool(const Symbol *)> fn = [](const Symbol *Symbol) {
      return !Symbol->isUndefined() && !Symbol->isReexported();
    };
    return make_filter_range(
        make_range<const_symbol_iterator>({Symbols.begin()}, {Symbols.end()}),
        fn);
  }

  // Range that contains all reexported symbols.
  const_filtered_symbol_range reexports() const {
    std::function<bool(const Symbol *)> fn = [](const Symbol *Symbol) {
      return Symbol->isReexported();
    };
    return make_filter_range(
        make_range<const_symbol_iterator>({Symbols.begin()}, {Symbols.end()}),
        fn);
  }

  // Range that contains all undefined and exported symbols.
  const_filtered_symbol_range undefineds() const {
    std::function<bool(const Symbol *)> fn = [](const Symbol *Symbol) {
      return Symbol->isUndefined();
    };
    return make_filter_range(
        make_range<const_symbol_iterator>({Symbols.begin()}, {Symbols.end()}),
        fn);
  }

  bool operator==(const SymbolSet &O) const;

  bool operator!=(const SymbolSet &O) const { return !(Symbols == O.Symbols); }

  void *allocate(size_t Size, unsigned Align = 8) {
    return Allocator.Allocate(Size, Align);
  }
};

} // namespace MachO
} // namespace llvm
#endif // LLVM_TEXTAPI_SYMBOLSET_H
