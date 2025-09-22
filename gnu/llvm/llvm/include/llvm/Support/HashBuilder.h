//===- llvm/Support/HashBuilder.h - Convenient hashing interface-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements an interface allowing to conveniently build hashes of
// various data types, without relying on the underlying hasher type to know
// about hashed data types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_HASHBUILDER_H
#define LLVM_SUPPORT_HASHBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/type_traits.h"

#include <iterator>
#include <optional>
#include <utility>

namespace llvm {

namespace hashbuilder_detail {
/// Trait to indicate whether a type's bits can be hashed directly (after
/// endianness correction).
template <typename U>
struct IsHashableData
    : std::integral_constant<bool, is_integral_or_enum<U>::value> {};

} // namespace hashbuilder_detail

/// Declares the hasher member, and functions forwarding directly to the hasher.
template <typename HasherT> class HashBuilderBase {
public:
  template <typename HasherT_ = HasherT>
  using HashResultTy = decltype(std::declval<HasherT_ &>().final());

  HasherT &getHasher() { return Hasher; }

  /// Forward to `HasherT::update(ArrayRef<uint8_t>)`.
  ///
  /// This may not take the size of `Data` into account.
  /// Users of this function should pay attention to respect endianness
  /// contraints.
  void update(ArrayRef<uint8_t> Data) { this->getHasher().update(Data); }

  /// Forward to `HasherT::update(ArrayRef<uint8_t>)`.
  ///
  /// This may not take the size of `Data` into account.
  /// Users of this function should pay attention to respect endianness
  /// contraints.
  void update(StringRef Data) {
    update(
        ArrayRef(reinterpret_cast<const uint8_t *>(Data.data()), Data.size()));
  }

  /// Forward to `HasherT::final()` if available.
  template <typename HasherT_ = HasherT> HashResultTy<HasherT_> final() {
    return this->getHasher().final();
  }

  /// Forward to `HasherT::result()` if available.
  template <typename HasherT_ = HasherT> HashResultTy<HasherT_> result() {
    return this->getHasher().result();
  }

protected:
  explicit HashBuilderBase(HasherT &Hasher) : Hasher(Hasher) {}

  template <typename... ArgTypes>
  explicit HashBuilderBase(ArgTypes &&...Args)
      : OptionalHasher(std::in_place, std::forward<ArgTypes>(Args)...),
        Hasher(*OptionalHasher) {}

private:
  std::optional<HasherT> OptionalHasher;
  HasherT &Hasher;
};

/// Interface to help hash various types through a hasher type.
///
/// Via provided specializations of `add`, `addRange`, and `addRangeElements`
/// functions, various types (e.g. `ArrayRef`, `StringRef`, etc.) can be hashed
/// without requiring any knowledge of hashed types from the hasher type.
///
/// The only method expected from the templated hasher type `HasherT` is:
/// * void update(ArrayRef<uint8_t> Data)
///
/// Additionally, the following methods will be forwarded to the hasher type:
/// * decltype(std::declval<HasherT &>().final()) final()
/// * decltype(std::declval<HasherT &>().result()) result()
///
/// From a user point of view, the interface provides the following:
/// * `template<typename T> add(const T &Value)`
///   The `add` function implements hashing of various types.
/// * `template <typename ItT> void addRange(ItT First, ItT Last)`
///   The `addRange` function is designed to aid hashing a range of values.
///   It explicitly adds the size of the range in the hash.
/// * `template <typename ItT> void addRangeElements(ItT First, ItT Last)`
///   The `addRangeElements` function is also designed to aid hashing a range of
///   values. In contrast to `addRange`, it **ignores** the size of the range,
///   behaving as if elements were added one at a time with `add`.
///
/// User-defined `struct` types can participate in this interface by providing
/// an `addHash` templated function. See the associated template specialization
/// for details.
///
/// This interface does not impose requirements on the hasher
/// `update(ArrayRef<uint8_t> Data)` method. We want to avoid collisions for
/// variable-size types; for example for
/// ```
/// builder.add({1});
/// builder.add({2, 3});
/// ```
/// and
/// ```
/// builder.add({1, 2});
/// builder.add({3});
/// ```
/// . Thus, specializations of `add` and `addHash` for variable-size types must
/// not assume that the hasher type considers the size as part of the hash; they
/// must explicitly add the size to the hash. See for example specializations
/// for `ArrayRef` and `StringRef`.
///
/// Additionally, since types are eventually forwarded to the hasher's
/// `void update(ArrayRef<uint8_t>)` method, endianness plays a role in the hash
/// computation (for example when computing `add((int)123)`).
/// Specifiying a non-`native` `Endianness` template parameter allows to compute
/// stable hash across platforms with different endianness.
template <typename HasherT, llvm::endianness Endianness>
class HashBuilder : public HashBuilderBase<HasherT> {
public:
  explicit HashBuilder(HasherT &Hasher) : HashBuilderBase<HasherT>(Hasher) {}
  template <typename... ArgTypes>
  explicit HashBuilder(ArgTypes &&...Args)
      : HashBuilderBase<HasherT>(Args...) {}

  /// Implement hashing for hashable data types, e.g. integral or enum values.
  template <typename T>
  std::enable_if_t<hashbuilder_detail::IsHashableData<T>::value, HashBuilder &>
  add(T Value) {
    return adjustForEndiannessAndAdd(Value);
  }

  /// Support hashing `ArrayRef`.
  ///
  /// `Value.size()` is taken into account to ensure cases like
  /// ```
  /// builder.add({1});
  /// builder.add({2, 3});
  /// ```
  /// and
  /// ```
  /// builder.add({1, 2});
  /// builder.add({3});
  /// ```
  /// do not collide.
  template <typename T> HashBuilder &add(ArrayRef<T> Value) {
    // As of implementation time, simply calling `addRange(Value)` would also go
    // through the `update` fast path. But that would rely on the implementation
    // details of `ArrayRef::begin()` and `ArrayRef::end()`. Explicitly call
    // `update` to guarantee the fast path.
    add(Value.size());
    if (hashbuilder_detail::IsHashableData<T>::value &&
        Endianness == llvm::endianness::native) {
      this->update(ArrayRef(reinterpret_cast<const uint8_t *>(Value.begin()),
                            Value.size() * sizeof(T)));
    } else {
      for (auto &V : Value)
        add(V);
    }
    return *this;
  }

  /// Support hashing `StringRef`.
  ///
  /// `Value.size()` is taken into account to ensure cases like
  /// ```
  /// builder.add("a");
  /// builder.add("bc");
  /// ```
  /// and
  /// ```
  /// builder.add("ab");
  /// builder.add("c");
  /// ```
  /// do not collide.
  HashBuilder &add(StringRef Value) {
    // As of implementation time, simply calling `addRange(Value)` would also go
    // through `update`. But that would rely on the implementation of
    // `StringRef::begin()` and `StringRef::end()`. Explicitly call `update` to
    // guarantee the fast path.
    add(Value.size());
    this->update(ArrayRef(reinterpret_cast<const uint8_t *>(Value.begin()),
                          Value.size()));
    return *this;
  }

  template <typename T>
  using HasAddHashT =
      decltype(addHash(std::declval<HashBuilder &>(), std::declval<T &>()));
  /// Implement hashing for user-defined `struct`s.
  ///
  /// Any user-define `struct` can participate in hashing via `HashBuilder` by
  /// providing a `addHash` templated function.
  ///
  /// ```
  /// template <typename HasherT, llvm::endianness Endianness>
  /// void addHash(HashBuilder<HasherT, Endianness> &HBuilder,
  ///              const UserDefinedStruct &Value);
  /// ```
  ///
  /// For example:
  /// ```
  /// struct SimpleStruct {
  ///   char c;
  ///   int i;
  /// };
  ///
  /// template <typename HasherT, llvm::endianness Endianness>
  /// void addHash(HashBuilder<HasherT, Endianness> &HBuilder,
  ///              const SimpleStruct &Value) {
  ///   HBuilder.add(Value.c);
  ///   HBuilder.add(Value.i);
  /// }
  /// ```
  ///
  /// To avoid endianness issues, specializations of `addHash` should
  /// generally rely on exising `add`, `addRange`, and `addRangeElements`
  /// functions. If directly using `update`, an implementation must correctly
  /// handle endianness.
  ///
  /// ```
  /// struct __attribute__ ((packed)) StructWithFastHash {
  ///   int I;
  ///   char C;
  ///
  ///   // If possible, we want to hash both `I` and `C` in a single
  ///   // `update` call for performance concerns.
  ///   template <typename HasherT, llvm::endianness Endianness>
  ///   friend void addHash(HashBuilder<HasherT, Endianness> &HBuilder,
  ///                       const StructWithFastHash &Value) {
  ///     if (Endianness == llvm::endianness::native) {
  ///       HBuilder.update(ArrayRef(
  ///           reinterpret_cast<const uint8_t *>(&Value), sizeof(Value)));
  ///     } else {
  ///       // Rely on existing `add` methods to handle endianness.
  ///       HBuilder.add(Value.I);
  ///       HBuilder.add(Value.C);
  ///     }
  ///   }
  /// };
  /// ```
  ///
  /// To avoid collisions, specialization of `addHash` for variable-size
  /// types must take the size into account.
  ///
  /// For example:
  /// ```
  /// struct CustomContainer {
  /// private:
  ///   size_t Size;
  ///   int Elements[100];
  ///
  /// public:
  ///   CustomContainer(size_t Size) : Size(Size) {
  ///     for (size_t I = 0; I != Size; ++I)
  ///       Elements[I] = I;
  ///   }
  ///   template <typename HasherT, llvm::endianness Endianness>
  ///   friend void addHash(HashBuilder<HasherT, Endianness> &HBuilder,
  ///                       const CustomContainer &Value) {
  ///     if (Endianness == llvm::endianness::native) {
  ///       HBuilder.update(ArrayRef(
  ///           reinterpret_cast<const uint8_t *>(&Value.Size),
  ///           sizeof(Value.Size) + Value.Size * sizeof(Value.Elements[0])));
  ///     } else {
  ///       // `addRange` will take care of encoding the size.
  ///       HBuilder.addRange(&Value.Elements[0], &Value.Elements[0] +
  ///       Value.Size);
  ///     }
  ///   }
  /// };
  /// ```
  template <typename T>
  std::enable_if_t<is_detected<HasAddHashT, T>::value &&
                       !hashbuilder_detail::IsHashableData<T>::value,
                   HashBuilder &>
  add(const T &Value) {
    addHash(*this, Value);
    return *this;
  }

  template <typename T1, typename T2>
  HashBuilder &add(const std::pair<T1, T2> &Value) {
    return add(Value.first, Value.second);
  }

  template <typename... Ts> HashBuilder &add(const std::tuple<Ts...> &Arg) {
    std::apply([this](const auto &...Args) { this->add(Args...); }, Arg);
    return *this;
  }

  /// A convenenience variadic helper.
  /// It simply iterates over its arguments, in order.
  /// ```
  /// add(Arg1, Arg2);
  /// ```
  /// is equivalent to
  /// ```
  /// add(Arg1)
  /// add(Arg2)
  /// ```
  template <typename... Ts>
  std::enable_if_t<(sizeof...(Ts) > 1), HashBuilder &> add(const Ts &...Args) {
    return (add(Args), ...);
  }

  template <typename ForwardIteratorT>
  HashBuilder &addRange(ForwardIteratorT First, ForwardIteratorT Last) {
    add(std::distance(First, Last));
    return addRangeElements(First, Last);
  }

  template <typename RangeT> HashBuilder &addRange(const RangeT &Range) {
    return addRange(adl_begin(Range), adl_end(Range));
  }

  template <typename ForwardIteratorT>
  HashBuilder &addRangeElements(ForwardIteratorT First, ForwardIteratorT Last) {
    return addRangeElementsImpl(
        First, Last,
        typename std::iterator_traits<ForwardIteratorT>::iterator_category());
  }

  template <typename RangeT>
  HashBuilder &addRangeElements(const RangeT &Range) {
    return addRangeElements(adl_begin(Range), adl_end(Range));
  }

  template <typename T>
  using HasByteSwapT = decltype(support::endian::byte_swap(
      std::declval<T &>(), llvm::endianness::little));
  /// Adjust `Value` for the target endianness and add it to the hash.
  template <typename T>
  std::enable_if_t<is_detected<HasByteSwapT, T>::value, HashBuilder &>
  adjustForEndiannessAndAdd(const T &Value) {
    T SwappedValue = support::endian::byte_swap(Value, Endianness);
    this->update(ArrayRef(reinterpret_cast<const uint8_t *>(&SwappedValue),
                          sizeof(SwappedValue)));
    return *this;
  }

private:
  // FIXME: Once available, specialize this function for `contiguous_iterator`s,
  // and use it for `ArrayRef` and `StringRef`.
  template <typename ForwardIteratorT>
  HashBuilder &addRangeElementsImpl(ForwardIteratorT First,
                                    ForwardIteratorT Last,
                                    std::forward_iterator_tag) {
    for (auto It = First; It != Last; ++It)
      add(*It);
    return *this;
  }

  template <typename T>
  std::enable_if_t<hashbuilder_detail::IsHashableData<T>::value &&
                       Endianness == llvm::endianness::native,
                   HashBuilder &>
  addRangeElementsImpl(T *First, T *Last, std::forward_iterator_tag) {
    this->update(ArrayRef(reinterpret_cast<const uint8_t *>(First),
                          (Last - First) * sizeof(T)));
    return *this;
  }
};

namespace hashbuilder_detail {
class HashCodeHasher {
public:
  HashCodeHasher() : Code(0) {}
  void update(ArrayRef<uint8_t> Data) {
    hash_code DataCode = hash_value(Data);
    Code = hash_combine(Code, DataCode);
  }
  hash_code Code;
};

using HashCodeHashBuilder =
    HashBuilder<hashbuilder_detail::HashCodeHasher, llvm::endianness::native>;
} // namespace hashbuilder_detail

/// Provide a default implementation of `hash_value` when `addHash(const T &)`
/// is supported.
template <typename T>
std::enable_if_t<
    is_detected<hashbuilder_detail::HashCodeHashBuilder::HasAddHashT, T>::value,
    hash_code>
hash_value(const T &Value) {
  hashbuilder_detail::HashCodeHashBuilder HBuilder;
  HBuilder.add(Value);
  return HBuilder.getHasher().Code;
}
} // end namespace llvm

#endif // LLVM_SUPPORT_HASHBUILDER_H
