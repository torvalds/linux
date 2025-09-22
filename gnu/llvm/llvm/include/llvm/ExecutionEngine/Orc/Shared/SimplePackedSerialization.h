//===---- SimplePackedSerialization.h - simple serialization ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The behavior of the utilities in this header must be synchronized with the
// behavior of the utilities in
// compiler-rt/lib/orc/simple_packed_serialization.h.
//
// The Simple Packed Serialization (SPS) utilities are used to generate
// argument and return buffers for wrapper functions using the following
// serialization scheme:
//
// Primitives (signed types should be two's complement):
//   bool, char, int8_t, uint8_t -- 8-bit (0=false, 1=true)
//   int16_t, uint16_t           -- 16-bit little endian
//   int32_t, uint32_t           -- 32-bit little endian
//   int64_t, int64_t            -- 64-bit little endian
//
// Sequence<T>:
//   Serialized as the sequence length (as a uint64_t) followed by the
//   serialization of each of the elements without padding.
//
// Tuple<T1, ..., TN>:
//   Serialized as each of the element types from T1 to TN without padding.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_SIMPLEPACKEDSERIALIZATION_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_SIMPLEPACKEDSERIALIZATION_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SwapByteOrder.h"

#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm {
namespace orc {
namespace shared {

/// Output char buffer with overflow check.
class SPSOutputBuffer {
public:
  SPSOutputBuffer(char *Buffer, size_t Remaining)
      : Buffer(Buffer), Remaining(Remaining) {}
  bool write(const char *Data, size_t Size) {
    assert(Data && "Data must not be null");
    if (Size > Remaining)
      return false;
    memcpy(Buffer, Data, Size);
    Buffer += Size;
    Remaining -= Size;
    return true;
  }

private:
  char *Buffer = nullptr;
  size_t Remaining = 0;
};

/// Input char buffer with underflow check.
class SPSInputBuffer {
public:
  SPSInputBuffer() = default;
  SPSInputBuffer(const char *Buffer, size_t Remaining)
      : Buffer(Buffer), Remaining(Remaining) {}
  bool read(char *Data, size_t Size) {
    if (Size > Remaining)
      return false;
    memcpy(Data, Buffer, Size);
    Buffer += Size;
    Remaining -= Size;
    return true;
  }

  const char *data() const { return Buffer; }
  bool skip(size_t Size) {
    if (Size > Remaining)
      return false;
    Buffer += Size;
    Remaining -= Size;
    return true;
  }

private:
  const char *Buffer = nullptr;
  size_t Remaining = 0;
};

/// Specialize to describe how to serialize/deserialize to/from the given
/// concrete type.
template <typename SPSTagT, typename ConcreteT, typename _ = void>
class SPSSerializationTraits;

/// A utility class for serializing to a blob from a variadic list.
template <typename... ArgTs> class SPSArgList;

// Empty list specialization for SPSArgList.
template <> class SPSArgList<> {
public:
  static size_t size() { return 0; }

  static bool serialize(SPSOutputBuffer &OB) { return true; }
  static bool deserialize(SPSInputBuffer &IB) { return true; }

  static bool serializeToSmallVector(SmallVectorImpl<char> &V) { return true; }

  static bool deserializeFromSmallVector(const SmallVectorImpl<char> &V) {
    return true;
  }
};

// Non-empty list specialization for SPSArgList.
template <typename SPSTagT, typename... SPSTagTs>
class SPSArgList<SPSTagT, SPSTagTs...> {
public:
  // FIXME: This typedef is here to enable SPS arg serialization from
  // JITLink. It can be removed once JITLink can access SPS directly.
  using OutputBuffer = SPSOutputBuffer;

  template <typename ArgT, typename... ArgTs>
  static size_t size(const ArgT &Arg, const ArgTs &...Args) {
    return SPSSerializationTraits<SPSTagT, ArgT>::size(Arg) +
           SPSArgList<SPSTagTs...>::size(Args...);
  }

  template <typename ArgT, typename... ArgTs>
  static bool serialize(SPSOutputBuffer &OB, const ArgT &Arg,
                        const ArgTs &...Args) {
    return SPSSerializationTraits<SPSTagT, ArgT>::serialize(OB, Arg) &&
           SPSArgList<SPSTagTs...>::serialize(OB, Args...);
  }

  template <typename ArgT, typename... ArgTs>
  static bool deserialize(SPSInputBuffer &IB, ArgT &Arg, ArgTs &...Args) {
    return SPSSerializationTraits<SPSTagT, ArgT>::deserialize(IB, Arg) &&
           SPSArgList<SPSTagTs...>::deserialize(IB, Args...);
  }
};

/// SPS serialization for integral types, bool, and char.
template <typename SPSTagT>
class SPSSerializationTraits<
    SPSTagT, SPSTagT,
    std::enable_if_t<std::is_same<SPSTagT, bool>::value ||
                     std::is_same<SPSTagT, char>::value ||
                     std::is_same<SPSTagT, int8_t>::value ||
                     std::is_same<SPSTagT, int16_t>::value ||
                     std::is_same<SPSTagT, int32_t>::value ||
                     std::is_same<SPSTagT, int64_t>::value ||
                     std::is_same<SPSTagT, uint8_t>::value ||
                     std::is_same<SPSTagT, uint16_t>::value ||
                     std::is_same<SPSTagT, uint32_t>::value ||
                     std::is_same<SPSTagT, uint64_t>::value>> {
public:
  static size_t size(const SPSTagT &Value) { return sizeof(SPSTagT); }

  static bool serialize(SPSOutputBuffer &OB, const SPSTagT &Value) {
    SPSTagT Tmp = Value;
    if (sys::IsBigEndianHost)
      sys::swapByteOrder(Tmp);
    return OB.write(reinterpret_cast<const char *>(&Tmp), sizeof(Tmp));
  }

  static bool deserialize(SPSInputBuffer &IB, SPSTagT &Value) {
    SPSTagT Tmp;
    if (!IB.read(reinterpret_cast<char *>(&Tmp), sizeof(Tmp)))
      return false;
    if (sys::IsBigEndianHost)
      sys::swapByteOrder(Tmp);
    Value = Tmp;
    return true;
  }
};

// Any empty placeholder suitable as a substitute for void when deserializing
class SPSEmpty {};

/// SPS tag type for tuples.
///
/// A blob tuple should be serialized by serializing each of the elements in
/// sequence.
template <typename... SPSTagTs> class SPSTuple {
public:
  /// Convenience typedef of the corresponding arg list.
  typedef SPSArgList<SPSTagTs...> AsArgList;
};

/// SPS tag type for optionals.
///
/// SPSOptionals should be serialized as a bool with true indicating that an
/// SPSTagT value is present, and false indicating that there is no value.
/// If the boolean is true then the serialized SPSTagT will follow immediately
/// after it.
template <typename SPSTagT> class SPSOptional {};

/// SPS tag type for sequences.
///
/// SPSSequences should be serialized as a uint64_t sequence length,
/// followed by the serialization of each of the elements.
template <typename SPSElementTagT> class SPSSequence;

/// SPS tag type for strings, which are equivalent to sequences of chars.
using SPSString = SPSSequence<char>;

/// SPS tag type for maps.
///
/// SPS maps are just sequences of (Key, Value) tuples.
template <typename SPSTagT1, typename SPSTagT2>
using SPSMap = SPSSequence<SPSTuple<SPSTagT1, SPSTagT2>>;

/// Serialization for SPSEmpty type.
template <> class SPSSerializationTraits<SPSEmpty, SPSEmpty> {
public:
  static size_t size(const SPSEmpty &EP) { return 0; }
  static bool serialize(SPSOutputBuffer &OB, const SPSEmpty &BE) {
    return true;
  }
  static bool deserialize(SPSInputBuffer &IB, SPSEmpty &BE) { return true; }
};

/// Specialize this to implement 'trivial' sequence serialization for
/// a concrete sequence type.
///
/// Trivial sequence serialization uses the sequence's 'size' member to get the
/// length of the sequence, and uses a range-based for loop to iterate over the
/// elements.
///
/// Specializing this template class means that you do not need to provide a
/// specialization of SPSSerializationTraits for your type.
template <typename SPSElementTagT, typename ConcreteSequenceT>
class TrivialSPSSequenceSerialization {
public:
  static constexpr bool available = false;
};

/// Specialize this to implement 'trivial' sequence deserialization for
/// a concrete sequence type.
///
/// Trivial deserialization calls a static 'reserve(SequenceT&)' method on your
/// specialization (you must implement this) to reserve space, and then calls
/// a static 'append(SequenceT&, ElementT&) method to append each of the
/// deserialized elements.
///
/// Specializing this template class means that you do not need to provide a
/// specialization of SPSSerializationTraits for your type.
template <typename SPSElementTagT, typename ConcreteSequenceT>
class TrivialSPSSequenceDeserialization {
public:
  static constexpr bool available = false;
};

/// Trivial std::string -> SPSSequence<char> serialization.
template <> class TrivialSPSSequenceSerialization<char, std::string> {
public:
  static constexpr bool available = true;
};

/// Trivial SPSSequence<char> -> std::string deserialization.
template <> class TrivialSPSSequenceDeserialization<char, std::string> {
public:
  static constexpr bool available = true;

  using element_type = char;

  static void reserve(std::string &S, uint64_t Size) { S.reserve(Size); }
  static bool append(std::string &S, char C) {
    S.push_back(C);
    return true;
  }
};

/// Trivial std::vector<T> -> SPSSequence<SPSElementTagT> serialization.
template <typename SPSElementTagT, typename T>
class TrivialSPSSequenceSerialization<SPSElementTagT, std::vector<T>> {
public:
  static constexpr bool available = true;
};

/// Trivial SPSSequence<SPSElementTagT> -> std::vector<T> deserialization.
template <typename SPSElementTagT, typename T>
class TrivialSPSSequenceDeserialization<SPSElementTagT, std::vector<T>> {
public:
  static constexpr bool available = true;

  using element_type = typename std::vector<T>::value_type;

  static void reserve(std::vector<T> &V, uint64_t Size) { V.reserve(Size); }
  static bool append(std::vector<T> &V, T E) {
    V.push_back(std::move(E));
    return true;
  }
};

/// Trivial SmallVectorImpl<T> -> SPSSequence<char> serialization.
template <typename SPSElementTagT, typename T>
class TrivialSPSSequenceSerialization<SPSElementTagT, SmallVectorImpl<T>> {
public:
  static constexpr bool available = true;
};

/// Trivial SPSSequence<SPSElementTagT> -> SmallVectorImpl<T> deserialization.
template <typename SPSElementTagT, typename T>
class TrivialSPSSequenceDeserialization<SPSElementTagT, SmallVectorImpl<T>> {
public:
  static constexpr bool available = true;

  using element_type = typename SmallVectorImpl<T>::value_type;

  static void reserve(SmallVectorImpl<T> &V, uint64_t Size) { V.reserve(Size); }
  static bool append(SmallVectorImpl<T> &V, T E) {
    V.push_back(std::move(E));
    return true;
  }
};

/// Trivial SmallVectorImpl<T> -> SPSSequence<char> serialization.
template <typename SPSElementTagT, typename T, unsigned N>
class TrivialSPSSequenceSerialization<SPSElementTagT, SmallVector<T, N>>
    : public TrivialSPSSequenceSerialization<SPSElementTagT,
                                             SmallVectorImpl<T>> {};

/// Trivial SPSSequence<SPSElementTagT> -> SmallVectorImpl<T> deserialization.
template <typename SPSElementTagT, typename T, unsigned N>
class TrivialSPSSequenceDeserialization<SPSElementTagT, SmallVector<T, N>>
    : public TrivialSPSSequenceDeserialization<SPSElementTagT,
                                               SmallVectorImpl<T>> {};

/// Trivial ArrayRef<T> -> SPSSequence<SPSElementTagT> serialization.
template <typename SPSElementTagT, typename T>
class TrivialSPSSequenceSerialization<SPSElementTagT, ArrayRef<T>> {
public:
  static constexpr bool available = true;
};

/// Specialized SPSSequence<char> -> ArrayRef<char> serialization.
///
/// On deserialize, points directly into the input buffer.
template <> class SPSSerializationTraits<SPSSequence<char>, ArrayRef<char>> {
public:
  static size_t size(const ArrayRef<char> &A) {
    return SPSArgList<uint64_t>::size(static_cast<uint64_t>(A.size())) +
           A.size();
  }

  static bool serialize(SPSOutputBuffer &OB, const ArrayRef<char> &A) {
    if (!SPSArgList<uint64_t>::serialize(OB, static_cast<uint64_t>(A.size())))
      return false;
    if (A.empty()) // Empty ArrayRef may have null data, so bail out early.
      return true;
    return OB.write(A.data(), A.size());
  }

  static bool deserialize(SPSInputBuffer &IB, ArrayRef<char> &A) {
    uint64_t Size;
    if (!SPSArgList<uint64_t>::deserialize(IB, Size))
      return false;
    if (Size > std::numeric_limits<size_t>::max())
      return false;
    A = {Size ? IB.data() : nullptr, static_cast<size_t>(Size)};
    return IB.skip(Size);
  }
};

/// 'Trivial' sequence serialization: Sequence is serialized as a uint64_t size
/// followed by a for-earch loop over the elements of the sequence to serialize
/// each of them.
template <typename SPSElementTagT, typename SequenceT>
class SPSSerializationTraits<SPSSequence<SPSElementTagT>, SequenceT,
                             std::enable_if_t<TrivialSPSSequenceSerialization<
                                 SPSElementTagT, SequenceT>::available>> {
public:
  static size_t size(const SequenceT &S) {
    size_t Size = SPSArgList<uint64_t>::size(static_cast<uint64_t>(S.size()));
    for (const auto &E : S)
      Size += SPSArgList<SPSElementTagT>::size(E);
    return Size;
  }

  static bool serialize(SPSOutputBuffer &OB, const SequenceT &S) {
    if (!SPSArgList<uint64_t>::serialize(OB, static_cast<uint64_t>(S.size())))
      return false;
    for (const auto &E : S)
      if (!SPSArgList<SPSElementTagT>::serialize(OB, E))
        return false;
    return true;
  }

  static bool deserialize(SPSInputBuffer &IB, SequenceT &S) {
    using TBSD = TrivialSPSSequenceDeserialization<SPSElementTagT, SequenceT>;
    uint64_t Size;
    if (!SPSArgList<uint64_t>::deserialize(IB, Size))
      return false;
    TBSD::reserve(S, Size);
    for (size_t I = 0; I != Size; ++I) {
      typename TBSD::element_type E;
      if (!SPSArgList<SPSElementTagT>::deserialize(IB, E))
        return false;
      if (!TBSD::append(S, std::move(E)))
        return false;
    }
    return true;
  }
};

/// SPSTuple serialization for std::tuple.
template <typename... SPSTagTs, typename... Ts>
class SPSSerializationTraits<SPSTuple<SPSTagTs...>, std::tuple<Ts...>> {
private:
  using TupleArgList = typename SPSTuple<SPSTagTs...>::AsArgList;
  using ArgIndices = std::make_index_sequence<sizeof...(Ts)>;

  template <std::size_t... I>
  static size_t size(const std::tuple<Ts...> &T, std::index_sequence<I...>) {
    return TupleArgList::size(std::get<I>(T)...);
  }

  template <std::size_t... I>
  static bool serialize(SPSOutputBuffer &OB, const std::tuple<Ts...> &T,
                        std::index_sequence<I...>) {
    return TupleArgList::serialize(OB, std::get<I>(T)...);
  }

  template <std::size_t... I>
  static bool deserialize(SPSInputBuffer &IB, std::tuple<Ts...> &T,
                          std::index_sequence<I...>) {
    return TupleArgList::deserialize(IB, std::get<I>(T)...);
  }

public:
  static size_t size(const std::tuple<Ts...> &T) {
    return size(T, ArgIndices{});
  }

  static bool serialize(SPSOutputBuffer &OB, const std::tuple<Ts...> &T) {
    return serialize(OB, T, ArgIndices{});
  }

  static bool deserialize(SPSInputBuffer &IB, std::tuple<Ts...> &T) {
    return deserialize(IB, T, ArgIndices{});
  }
};

/// SPSTuple serialization for std::pair.
template <typename SPSTagT1, typename SPSTagT2, typename T1, typename T2>
class SPSSerializationTraits<SPSTuple<SPSTagT1, SPSTagT2>, std::pair<T1, T2>> {
public:
  static size_t size(const std::pair<T1, T2> &P) {
    return SPSArgList<SPSTagT1>::size(P.first) +
           SPSArgList<SPSTagT2>::size(P.second);
  }

  static bool serialize(SPSOutputBuffer &OB, const std::pair<T1, T2> &P) {
    return SPSArgList<SPSTagT1>::serialize(OB, P.first) &&
           SPSArgList<SPSTagT2>::serialize(OB, P.second);
  }

  static bool deserialize(SPSInputBuffer &IB, std::pair<T1, T2> &P) {
    return SPSArgList<SPSTagT1>::deserialize(IB, P.first) &&
           SPSArgList<SPSTagT2>::deserialize(IB, P.second);
  }
};

/// SPSOptional serialization for std::optional.
template <typename SPSTagT, typename T>
class SPSSerializationTraits<SPSOptional<SPSTagT>, std::optional<T>> {
public:
  static size_t size(const std::optional<T> &Value) {
    size_t Size = SPSArgList<bool>::size(!!Value);
    if (Value)
      Size += SPSArgList<SPSTagT>::size(*Value);
    return Size;
  }

  static bool serialize(SPSOutputBuffer &OB, const std::optional<T> &Value) {
    if (!SPSArgList<bool>::serialize(OB, !!Value))
      return false;
    if (Value)
      return SPSArgList<SPSTagT>::serialize(OB, *Value);
    return true;
  }

  static bool deserialize(SPSInputBuffer &IB, std::optional<T> &Value) {
    bool HasValue;
    if (!SPSArgList<bool>::deserialize(IB, HasValue))
      return false;
    if (HasValue) {
      Value = T();
      return SPSArgList<SPSTagT>::deserialize(IB, *Value);
    } else
      Value = std::optional<T>();
    return true;
  }
};

/// Serialization for StringRefs.
///
/// Serialization is as for regular strings. Deserialization points directly
/// into the blob.
template <> class SPSSerializationTraits<SPSString, StringRef> {
public:
  static size_t size(const StringRef &S) {
    return SPSArgList<uint64_t>::size(static_cast<uint64_t>(S.size())) +
           S.size();
  }

  static bool serialize(SPSOutputBuffer &OB, StringRef S) {
    if (!SPSArgList<uint64_t>::serialize(OB, static_cast<uint64_t>(S.size())))
      return false;
    if (S.empty()) // Empty StringRef may have null data, so bail out early.
      return true;
    return OB.write(S.data(), S.size());
  }

  static bool deserialize(SPSInputBuffer &IB, StringRef &S) {
    const char *Data = nullptr;
    uint64_t Size;
    if (!SPSArgList<uint64_t>::deserialize(IB, Size))
      return false;
    Data = IB.data();
    if (!IB.skip(Size))
      return false;
    S = StringRef(Size ? Data : nullptr, Size);
    return true;
  }
};

/// Serialization for StringMap<ValueT>s.
template <typename SPSValueT, typename ValueT>
class SPSSerializationTraits<SPSSequence<SPSTuple<SPSString, SPSValueT>>,
                             StringMap<ValueT>> {
public:
  static size_t size(const StringMap<ValueT> &M) {
    size_t Sz = SPSArgList<uint64_t>::size(static_cast<uint64_t>(M.size()));
    for (auto &E : M)
      Sz += SPSArgList<SPSString, SPSValueT>::size(E.first(), E.second);
    return Sz;
  }

  static bool serialize(SPSOutputBuffer &OB, const StringMap<ValueT> &M) {
    if (!SPSArgList<uint64_t>::serialize(OB, static_cast<uint64_t>(M.size())))
      return false;

    for (auto &E : M)
      if (!SPSArgList<SPSString, SPSValueT>::serialize(OB, E.first(), E.second))
        return false;

    return true;
  }

  static bool deserialize(SPSInputBuffer &IB, StringMap<ValueT> &M) {
    uint64_t Size;
    assert(M.empty() && "M already contains elements");

    if (!SPSArgList<uint64_t>::deserialize(IB, Size))
      return false;

    while (Size--) {
      StringRef S;
      ValueT V;
      if (!SPSArgList<SPSString, SPSValueT>::deserialize(IB, S, V))
        return false;
      if (!M.insert(std::make_pair(S, V)).second)
        return false;
    }

    return true;
  }
};

/// SPS tag type for errors.
class SPSError;

/// SPS tag type for expecteds, which are either a T or a string representing
/// an error.
template <typename SPSTagT> class SPSExpected;

namespace detail {

/// Helper type for serializing Errors.
///
/// llvm::Errors are move-only, and not inspectable except by consuming them.
/// This makes them unsuitable for direct serialization via
/// SPSSerializationTraits, which needs to inspect values twice (once to
/// determine the amount of space to reserve, and then again to serialize).
///
/// The SPSSerializableError type is a helper that can be
/// constructed from an llvm::Error, but inspected more than once.
struct SPSSerializableError {
  bool HasError = false;
  std::string ErrMsg;
};

/// Helper type for serializing Expected<T>s.
///
/// See SPSSerializableError for more details.
///
// FIXME: Use std::variant for storage once we have c++17.
template <typename T> struct SPSSerializableExpected {
  bool HasValue = false;
  T Value{};
  std::string ErrMsg;
};

inline SPSSerializableError toSPSSerializable(Error Err) {
  if (Err)
    return {true, toString(std::move(Err))};
  return {false, {}};
}

inline Error fromSPSSerializable(SPSSerializableError BSE) {
  if (BSE.HasError)
    return make_error<StringError>(BSE.ErrMsg, inconvertibleErrorCode());
  return Error::success();
}

template <typename T>
SPSSerializableExpected<T> toSPSSerializable(Expected<T> E) {
  if (E)
    return {true, std::move(*E), {}};
  else
    return {false, T(), toString(E.takeError())};
}

template <typename T>
Expected<T> fromSPSSerializable(SPSSerializableExpected<T> BSE) {
  if (BSE.HasValue)
    return std::move(BSE.Value);
  else
    return make_error<StringError>(BSE.ErrMsg, inconvertibleErrorCode());
}

} // end namespace detail

/// Serialize to a SPSError from a detail::SPSSerializableError.
template <>
class SPSSerializationTraits<SPSError, detail::SPSSerializableError> {
public:
  static size_t size(const detail::SPSSerializableError &BSE) {
    size_t Size = SPSArgList<bool>::size(BSE.HasError);
    if (BSE.HasError)
      Size += SPSArgList<SPSString>::size(BSE.ErrMsg);
    return Size;
  }

  static bool serialize(SPSOutputBuffer &OB,
                        const detail::SPSSerializableError &BSE) {
    if (!SPSArgList<bool>::serialize(OB, BSE.HasError))
      return false;
    if (BSE.HasError)
      if (!SPSArgList<SPSString>::serialize(OB, BSE.ErrMsg))
        return false;
    return true;
  }

  static bool deserialize(SPSInputBuffer &IB,
                          detail::SPSSerializableError &BSE) {
    if (!SPSArgList<bool>::deserialize(IB, BSE.HasError))
      return false;

    if (!BSE.HasError)
      return true;

    return SPSArgList<SPSString>::deserialize(IB, BSE.ErrMsg);
  }
};

/// Serialize to a SPSExpected<SPSTagT> from a
/// detail::SPSSerializableExpected<T>.
template <typename SPSTagT, typename T>
class SPSSerializationTraits<SPSExpected<SPSTagT>,
                             detail::SPSSerializableExpected<T>> {
public:
  static size_t size(const detail::SPSSerializableExpected<T> &BSE) {
    size_t Size = SPSArgList<bool>::size(BSE.HasValue);
    if (BSE.HasValue)
      Size += SPSArgList<SPSTagT>::size(BSE.Value);
    else
      Size += SPSArgList<SPSString>::size(BSE.ErrMsg);
    return Size;
  }

  static bool serialize(SPSOutputBuffer &OB,
                        const detail::SPSSerializableExpected<T> &BSE) {
    if (!SPSArgList<bool>::serialize(OB, BSE.HasValue))
      return false;

    if (BSE.HasValue)
      return SPSArgList<SPSTagT>::serialize(OB, BSE.Value);

    return SPSArgList<SPSString>::serialize(OB, BSE.ErrMsg);
  }

  static bool deserialize(SPSInputBuffer &IB,
                          detail::SPSSerializableExpected<T> &BSE) {
    if (!SPSArgList<bool>::deserialize(IB, BSE.HasValue))
      return false;

    if (BSE.HasValue)
      return SPSArgList<SPSTagT>::deserialize(IB, BSE.Value);

    return SPSArgList<SPSString>::deserialize(IB, BSE.ErrMsg);
  }
};

/// Serialize to a SPSExpected<SPSTagT> from a detail::SPSSerializableError.
template <typename SPSTagT>
class SPSSerializationTraits<SPSExpected<SPSTagT>,
                             detail::SPSSerializableError> {
public:
  static size_t size(const detail::SPSSerializableError &BSE) {
    assert(BSE.HasError && "Cannot serialize expected from a success value");
    return SPSArgList<bool>::size(false) +
           SPSArgList<SPSString>::size(BSE.ErrMsg);
  }

  static bool serialize(SPSOutputBuffer &OB,
                        const detail::SPSSerializableError &BSE) {
    assert(BSE.HasError && "Cannot serialize expected from a success value");
    if (!SPSArgList<bool>::serialize(OB, false))
      return false;
    return SPSArgList<SPSString>::serialize(OB, BSE.ErrMsg);
  }
};

/// Serialize to a SPSExpected<SPSTagT> from a T.
template <typename SPSTagT, typename T>
class SPSSerializationTraits<SPSExpected<SPSTagT>, T> {
public:
  static size_t size(const T &Value) {
    return SPSArgList<bool>::size(true) + SPSArgList<SPSTagT>::size(Value);
  }

  static bool serialize(SPSOutputBuffer &OB, const T &Value) {
    if (!SPSArgList<bool>::serialize(OB, true))
      return false;
    return SPSArgList<SPSTagT>::serialize(Value);
  }
};

} // end namespace shared
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_SIMPLEPACKEDSERIALIZATION_H
