//===--- HashKeyMap.h - Wrapper for maps using hash value key ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Defines HashKeyMap template.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_HASHKEYMAP_H
#define LLVM_PROFILEDATA_HASHKEYMAP_H

#include "llvm/ADT/Hashing.h"
#include <iterator>
#include <utility>

namespace llvm {

namespace sampleprof {

/// This class is a wrapper to associative container MapT<KeyT, ValueT> using
/// the hash value of the original key as the new key. This greatly improves the
/// performance of insert and query operations especially when hash values of
/// keys are available a priori, and reduces memory usage if KeyT has a large
/// size.
/// All keys with the same hash value are considered equivalent (i.e. hash
/// collision is silently ignored). Given such feature this class should only be
/// used where it does not affect compilation correctness, for example, when
/// loading a sample profile. The original key is not stored, so if the user
/// needs to preserve it, it should be stored in the mapped type.
/// Assuming the hashing algorithm is uniform, we use the formula
/// 1 - Permute(n, k) / n ^ k where n is the universe size and k is number of
/// elements chosen at random to calculate the probability of collision. With
/// 1,000,000 entries the probability is negligible:
/// 1 - (2^64)!/((2^64-1000000)!*(2^64)^1000000) ~= 3*10^-8.
/// Source: https://en.wikipedia.org/wiki/Birthday_problem
///
/// \param MapT The underlying associative container type.
/// \param KeyT The original key type, which requires the implementation of
///   llvm::hash_value(KeyT).
/// \param ValueT The original mapped type, which has the same requirement as
///   the underlying container.
/// \param MapTArgs Additional template parameters passed to the underlying
///   container.
template <template <typename, typename, typename...> typename MapT,
          typename KeyT, typename ValueT, typename... MapTArgs>
class HashKeyMap :
    public MapT<decltype(hash_value(KeyT())), ValueT, MapTArgs...> {
public:
  using base_type = MapT<decltype(hash_value(KeyT())), ValueT, MapTArgs...>;
  using key_type = decltype(hash_value(KeyT()));
  using original_key_type = KeyT;
  using mapped_type = ValueT;
  using value_type = typename base_type::value_type;

  using iterator = typename base_type::iterator;
  using const_iterator = typename base_type::const_iterator;

  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const key_type &Hash,
                                        const original_key_type &Key,
                                        Ts &&...Args) {
    assert(Hash == hash_value(Key));
    return base_type::try_emplace(Hash, std::forward<Ts>(Args)...);
  }

  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const original_key_type &Key,
                                        Ts &&...Args) {
    return try_emplace(hash_value(Key), Key, std::forward<Ts>(Args)...);
  }

  template <typename... Ts> std::pair<iterator, bool> emplace(Ts &&...Args) {
    return try_emplace(std::forward<Ts>(Args)...);
  }

  mapped_type &operator[](const original_key_type &Key) {
    return try_emplace(Key, mapped_type()).first->second;
  }

  iterator find(const original_key_type &Key) {
    auto It = base_type::find(hash_value(Key));
    if (It != base_type::end())
      return It;
    return base_type::end();
  }

  const_iterator find(const original_key_type &Key) const {
    auto It = base_type::find(hash_value(Key));
    if (It != base_type::end())
      return It;
    return base_type::end();
  }

  mapped_type lookup(const original_key_type &Key) const {
    auto It = base_type::find(hash_value(Key));
    if (It != base_type::end())
      return It->second;
    return mapped_type();
  }

  size_t count(const original_key_type &Key) const {
    return base_type::count(hash_value(Key));
  }

  size_t erase(const original_key_type &Ctx) {
    auto It = find(Ctx);
    if (It != base_type::end()) {
      base_type::erase(It);
      return 1;
    }
    return 0;
  }

  iterator erase(const_iterator It) {
    return base_type::erase(It);
  }
};

}

}

#endif // LLVM_PROFILEDATA_HASHKEYMAP_H
