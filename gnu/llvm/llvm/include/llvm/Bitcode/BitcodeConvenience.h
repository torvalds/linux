//===- llvm/Bitcode/BitcodeConvenience.h - Convenience Wrappers -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file Convenience wrappers for the LLVM bitcode format and bitstream APIs.
///
/// This allows you to use a sort of DSL to declare and use bitcode
/// abbreviations and records. Example:
///
/// \code
///     using Metadata = BCRecordLayout<
///       METADATA_ID,  // ID
///       BCFixed<16>,  // Module format major version
///       BCFixed<16>,  // Module format minor version
///       BCBlob        // misc. version information
///     >;
///     Metadata metadata(Out);
///     metadata.emit(ScratchRecord, VERSION_MAJOR, VERSION_MINOR, Data);
/// \endcode
///
/// For details on the bitcode format, see
///   http://llvm.org/docs/BitCodeFormat.html
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODECONVENIENCE_H
#define LLVM_BITCODE_BITCODECONVENIENCE_H

#include "llvm/Bitstream/BitCodes.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include <cstdint>
#include <optional>

namespace llvm {
namespace detail {
/// Convenience base for all kinds of bitcode abbreviation fields.
///
/// This just defines common properties queried by the metaprogramming.
template <bool Compound = false> class BCField {
public:
  static const bool IsCompound = Compound;

  /// Asserts that the given data is a valid value for this field.
  template <typename T> static void assertValid(const T &data) {}

  /// Converts a raw numeric representation of this value to its preferred
  /// type.
  template <typename T> static T convert(T rawValue) { return rawValue; }
};
} // namespace detail

/// Represents a literal operand in a bitcode record.
///
/// The value of a literal operand is the same for all instances of the record,
/// so it is only emitted in the abbreviation definition.
///
/// Note that because this uses a compile-time template, you cannot have a
/// literal operand that is fixed at run-time without dropping down to the
/// raw LLVM APIs.
template <uint64_t Value> class BCLiteral : public detail::BCField<> {
public:
  static void emitOp(llvm::BitCodeAbbrev &abbrev) {
    abbrev.Add(llvm::BitCodeAbbrevOp(Value));
  }

  template <typename T> static void assertValid(const T &data) {
    assert(data == Value && "data value does not match declared literal value");
  }
};

/// Represents a fixed-width value in a bitcode record.
///
/// Note that the LLVM bitcode format only supports unsigned values.
template <unsigned Width> class BCFixed : public detail::BCField<> {
public:
  static_assert(Width <= 64, "fixed-width field is too large");

  static void emitOp(llvm::BitCodeAbbrev &abbrev) {
    abbrev.Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Fixed, Width));
  }

  static void assertValid(const bool &data) {
    assert(llvm::isUInt<Width>(data) &&
           "data value does not fit in the given bit width");
  }

  template <typename T> static void assertValid(const T &data) {
    assert(data >= 0 && "cannot encode signed integers");
    assert(llvm::isUInt<Width>(data) &&
           "data value does not fit in the given bit width");
  }
};

/// Represents a variable-width value in a bitcode record.
///
/// The \p Width parameter should include the continuation bit.
///
/// Note that the LLVM bitcode format only supports unsigned values.
template <unsigned Width> class BCVBR : public detail::BCField<> {
  static_assert(Width >= 2, "width does not have room for continuation bit");

public:
  static void emitOp(llvm::BitCodeAbbrev &abbrev) {
    abbrev.Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, Width));
  }

  template <typename T> static void assertValid(const T &data) {
    assert(data >= 0 && "cannot encode signed integers");
  }
};

/// Represents a character encoded in LLVM's Char6 encoding.
///
/// This format is suitable for encoding decimal numbers (without signs or
/// exponents) and C identifiers (without dollar signs), but not much else.
///
/// \sa http://llvm.org/docs/BitCodeFormat.html#char6-encoded-value
class BCChar6 : public detail::BCField<> {
public:
  static void emitOp(llvm::BitCodeAbbrev &abbrev) {
    abbrev.Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Char6));
  }

  template <typename T> static void assertValid(const T &data) {
    assert(llvm::BitCodeAbbrevOp::isChar6(data) && "invalid Char6 data");
  }

  template <typename T> char convert(T rawValue) {
    return static_cast<char>(rawValue);
  }
};

/// Represents an untyped blob of bytes.
///
/// If present, this must be the last field in a record.
class BCBlob : public detail::BCField<true> {
public:
  static void emitOp(llvm::BitCodeAbbrev &abbrev) {
    abbrev.Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Blob));
  }
};

/// Represents an array of some other type.
///
/// If present, this must be the last field in a record.
template <typename ElementTy> class BCArray : public detail::BCField<true> {
  static_assert(!ElementTy::IsCompound, "arrays can only contain scalar types");

public:
  static void emitOp(llvm::BitCodeAbbrev &abbrev) {
    abbrev.Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Array));
    ElementTy::emitOp(abbrev);
  }
};

namespace detail {
/// Attaches the last field to an abbreviation.
///
/// This is the base case for \c emitOps.
///
/// \sa BCRecordLayout::emitAbbrev
template <typename FieldTy> static void emitOps(llvm::BitCodeAbbrev &abbrev) {
  FieldTy::emitOp(abbrev);
}

/// Attaches fields to an abbreviation.
///
/// This is the recursive case for \c emitOps.
///
/// \sa BCRecordLayout::emitAbbrev
template <typename FieldTy, typename Next, typename... Rest>
static void emitOps(llvm::BitCodeAbbrev &abbrev) {
  static_assert(!FieldTy::IsCompound,
                "arrays and blobs may not appear in the middle of a record");
  FieldTy::emitOp(abbrev);
  emitOps<Next, Rest...>(abbrev);
}

/// Helper class for dealing with a scalar element in the middle of a record.
///
/// \sa BCRecordLayout
template <typename ElementTy, typename... Fields> class BCRecordCoding {
public:
  template <typename BufferTy, typename ElementDataTy, typename... DataTy>
  static void emit(llvm::BitstreamWriter &Stream, BufferTy &buffer,
                   unsigned code, ElementDataTy element, DataTy &&...data) {
    static_assert(!ElementTy::IsCompound,
                  "arrays and blobs may not appear in the middle of a record");
    ElementTy::assertValid(element);
    buffer.push_back(element);
    BCRecordCoding<Fields...>::emit(Stream, buffer, code,
                                    std::forward<DataTy>(data)...);
  }

  template <typename T, typename ElementDataTy, typename... DataTy>
  static void read(ArrayRef<T> buffer, ElementDataTy &element,
                   DataTy &&...data) {
    assert(!buffer.empty() && "too few elements in buffer");
    element = ElementTy::convert(buffer.front());
    BCRecordCoding<Fields...>::read(buffer.slice(1),
                                    std::forward<DataTy>(data)...);
  }

  template <typename T, typename... DataTy>
  static void read(ArrayRef<T> buffer, std::nullopt_t, DataTy &&...data) {
    assert(!buffer.empty() && "too few elements in buffer");
    BCRecordCoding<Fields...>::read(buffer.slice(1),
                                    std::forward<DataTy>(data)...);
  }
};

/// Helper class for dealing with a scalar element at the end of a record.
///
/// This has a separate implementation because up until now we've only been
/// \em building the record (into a data buffer), and now we need to hand it
/// off to the BitstreamWriter to be emitted.
///
/// \sa BCRecordLayout
template <typename ElementTy> class BCRecordCoding<ElementTy> {
public:
  template <typename BufferTy, typename DataTy>
  static void emit(llvm::BitstreamWriter &Stream, BufferTy &buffer,
                   unsigned code, const DataTy &data) {
    static_assert(!ElementTy::IsCompound,
                  "arrays and blobs need special handling");
    ElementTy::assertValid(data);
    buffer.push_back(data);
    Stream.EmitRecordWithAbbrev(code, buffer);
  }

  template <typename T, typename DataTy>
  static void read(ArrayRef<T> buffer, DataTy &data) {
    assert(buffer.size() == 1 && "record data does not match layout");
    data = ElementTy::convert(buffer.front());
  }

  template <typename T> static void read(ArrayRef<T> buffer, std::nullopt_t) {
    assert(buffer.size() == 1 && "record data does not match layout");
    (void)buffer;
  }

  template <typename T> static void read(ArrayRef<T> buffer) = delete;
};

/// Helper class for dealing with an array at the end of a record.
///
/// \sa BCRecordLayout::emitRecord
template <typename ElementTy> class BCRecordCoding<BCArray<ElementTy>> {
public:
  template <typename BufferTy>
  static void emit(llvm::BitstreamWriter &Stream, BufferTy &buffer,
                   unsigned code, StringRef data) {
    // TODO: validate array data.
    Stream.EmitRecordWithArray(code, buffer, data);
  }

  template <typename BufferTy, typename ArrayTy>
  static void emit(llvm::BitstreamWriter &Stream, BufferTy &buffer,
                   unsigned code, const ArrayTy &array) {
#ifndef NDEBUG
    for (auto &element : array)
      ElementTy::assertValid(element);
#endif
    buffer.reserve(buffer.size() + std::distance(array.begin(), array.end()));
    std::copy(array.begin(), array.end(), std::back_inserter(buffer));
    Stream.EmitRecordWithAbbrev(code, buffer);
  }

  template <typename BufferTy, typename ElementDataTy, typename... DataTy>
  static void emit(llvm::BitstreamWriter &Stream, BufferTy &buffer,
                   unsigned code, ElementDataTy element, DataTy... data) {
    std::array<ElementDataTy, 1 + sizeof...(data)> array{{element, data...}};
    emit(Stream, buffer, code, array);
  }

  template <typename BufferTy>
  static void emit(llvm::BitstreamWriter &Stream, BufferTy &Buffer,
                   unsigned code, std::nullopt_t) {
    Stream.EmitRecordWithAbbrev(code, Buffer);
  }

  template <typename T>
  static void read(ArrayRef<T> Buffer, ArrayRef<T> &rawData) {
    rawData = Buffer;
  }

  template <typename T, typename ArrayTy>
  static void read(ArrayRef<T> buffer, ArrayTy &array) {
    array.append(llvm::map_iterator(buffer.begin(), T::convert),
                 llvm::map_iterator(buffer.end(), T::convert));
  }

  template <typename T> static void read(ArrayRef<T> buffer, std::nullopt_t) {
    (void)buffer;
  }

  template <typename T> static void read(ArrayRef<T> buffer) = delete;
};

/// Helper class for dealing with a blob at the end of a record.
///
/// \sa BCRecordLayout
template <> class BCRecordCoding<BCBlob> {
public:
  template <typename BufferTy>
  static void emit(llvm::BitstreamWriter &Stream, BufferTy &buffer,
                   unsigned code, StringRef data) {
    Stream.EmitRecordWithBlob(code, buffer, data);
  }

  template <typename T> static void read(ArrayRef<T> buffer) { (void)buffer; }

  /// Blob data is not stored in the buffer if you are using the correct
  /// accessor; this method should not be used.
  template <typename T, typename DataTy>
  static void read(ArrayRef<T> buffer, DataTy &data) = delete;
};

/// A type trait whose \c type field is the last of its template parameters.
template <typename Head, typename... Tail> struct last_type {
  using type = typename last_type<Tail...>::type;
};

template <typename Head> struct last_type<Head> { using type = Head; };

/// A type trait whose \c value field is \c true if the last type is BCBlob.
template <typename... Types>
using has_blob = std::is_same<BCBlob, typename last_type<int, Types...>::type>;

/// A type trait whose \c value field is \c true if the given type is a
/// BCArray (of any element kind).
template <typename T> struct is_array {
private:
  template <typename E> static bool check(BCArray<E> *);
  static int check(...);

public:
  typedef bool value_type;
  static constexpr bool value = !std::is_same<decltype(check((T *)nullptr)),
                                              decltype(check(false))>::value;
};

/// A type trait whose \c value field is \c true if the last type is a
/// BCArray (of any element kind).
template <typename... Types>
using has_array = is_array<typename last_type<int, Types...>::type>;
} // namespace detail

/// Represents a single bitcode record type.
///
/// This class template is meant to be instantiated and then given a name,
/// so that from then on that name can be used.
template <typename IDField, typename... Fields> class BCGenericRecordLayout {
  llvm::BitstreamWriter &Stream;

public:
  /// The abbreviation code used for this record in the current block.
  ///
  /// Note that this is not the same as the semantic record code, which is the
  /// first field of the record.
  const unsigned AbbrevCode;

  /// Create a layout and register it with the given bitstream writer.
  explicit BCGenericRecordLayout(llvm::BitstreamWriter &Stream)
      : Stream(Stream), AbbrevCode(emitAbbrev(Stream)) {}

  /// Emit a record to the bitstream writer, using the given buffer for scratch
  /// space.
  ///
  /// Note that even fixed arguments must be specified here.
  template <typename BufferTy, typename... Data>
  void emit(BufferTy &buffer, unsigned id, Data &&...data) const {
    emitRecord(Stream, buffer, AbbrevCode, id, std::forward<Data>(data)...);
  }

  /// Registers this record's layout with the bitstream reader.
  ///
  /// eturns The abbreviation code for the newly-registered record type.
  static unsigned emitAbbrev(llvm::BitstreamWriter &Stream) {
    auto Abbrev = std::make_shared<llvm::BitCodeAbbrev>();
    detail::emitOps<IDField, Fields...>(*Abbrev);
    return Stream.EmitAbbrev(std::move(Abbrev));
  }

  /// Emit a record identified by \p abbrCode to bitstream reader \p Stream,
  /// using \p buffer for scratch space.
  ///
  /// Note that even fixed arguments must be specified here. Blobs are passed
  /// as StringRefs, while arrays can be passed inline, as aggregates, or as
  /// pre-encoded StringRef data. Skipped values and empty arrays should use
  /// the special Nothing value.
  template <typename BufferTy, typename... Data>
  static void emitRecord(llvm::BitstreamWriter &Stream, BufferTy &buffer,
                         unsigned abbrCode, unsigned recordID, Data &&...data) {
    static_assert(sizeof...(data) <= sizeof...(Fields) ||
                      detail::has_array<Fields...>::value,
                  "Too many record elements");
    static_assert(sizeof...(data) >= sizeof...(Fields),
                  "Too few record elements");
    buffer.clear();
    detail::BCRecordCoding<IDField, Fields...>::emit(
        Stream, buffer, abbrCode, recordID, std::forward<Data>(data)...);
  }

  /// Extract record data from \p buffer into the given data fields.
  ///
  /// Note that even fixed arguments must be specified here. Pass \c Nothing
  /// if you don't care about a particular parameter. Blob data is not included
  /// in the buffer and should be handled separately by the caller.
  template <typename ElementTy, typename... Data>
  static void readRecord(ArrayRef<ElementTy> buffer, Data &&...data) {
    static_assert(sizeof...(data) <= sizeof...(Fields),
                  "Too many record elements");
    static_assert(sizeof...(Fields) <=
                      sizeof...(data) + detail::has_blob<Fields...>::value,
                  "Too few record elements");
    return detail::BCRecordCoding<Fields...>::read(buffer,
                                                   std::forward<Data>(data)...);
  }

  /// Extract record data from \p buffer into the given data fields.
  ///
  /// Note that even fixed arguments must be specified here. Pass \c Nothing
  /// if you don't care about a particular parameter. Blob data is not included
  /// in the buffer and should be handled separately by the caller.
  template <typename BufferTy, typename... Data>
  static void readRecord(BufferTy &buffer, Data &&...data) {
    return readRecord(llvm::ArrayRef(buffer), std::forward<Data>(data)...);
  }
};

/// A record with a fixed record code.
template <unsigned RecordCode, typename... Fields>
class BCRecordLayout
    : public BCGenericRecordLayout<BCLiteral<RecordCode>, Fields...> {
  using Base = BCGenericRecordLayout<BCLiteral<RecordCode>, Fields...>;

public:
  enum : unsigned {
    /// The record code associated with this layout.
    Code = RecordCode
  };

  /// Create a layout and register it with the given bitstream writer.
  explicit BCRecordLayout(llvm::BitstreamWriter &Stream) : Base(Stream) {}

  /// Emit a record to the bitstream writer, using the given buffer for scratch
  /// space.
  ///
  /// Note that even fixed arguments must be specified here.
  template <typename BufferTy, typename... Data>
  void emit(BufferTy &buffer, Data &&...data) const {
    Base::emit(buffer, RecordCode, std::forward<Data>(data)...);
  }

  /// Emit a record identified by \p abbrCode to bitstream reader \p Stream,
  /// using \p buffer for scratch space.
  ///
  /// Note that even fixed arguments must be specified here. Currently, arrays
  /// and blobs can only be passed as StringRefs.
  template <typename BufferTy, typename... Data>
  static void emitRecord(llvm::BitstreamWriter &Stream, BufferTy &buffer,
                         unsigned abbrCode, Data &&...data) {
    Base::emitRecord(Stream, buffer, abbrCode, RecordCode,
                     std::forward<Data>(data)...);
  }
};

/// RAII object to pair entering and exiting a sub-block.
class BCBlockRAII {
  llvm::BitstreamWriter &Stream;

public:
  BCBlockRAII(llvm::BitstreamWriter &Stream, unsigned block, unsigned abbrev)
      : Stream(Stream) {
    Stream.EnterSubblock(block, abbrev);
  }

  ~BCBlockRAII() { Stream.ExitBlock(); }
};
} // namespace llvm

#endif
