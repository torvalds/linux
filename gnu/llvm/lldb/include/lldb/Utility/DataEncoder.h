//===-- DataEncoder.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_DATAENCODER_H
#define LLDB_UTILITY_DATAENCODER_H

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <cstddef>
#include <cstdint>

namespace lldb_private {

/// \class DataEncoder
///
/// An binary data encoding class.
///
/// DataEncoder is a class that can encode binary data (swapping if needed) to
/// a data buffer. The DataEncoder can be constructed with data that will be
/// copied into the internally owned buffer. This allows data to be modified
/// in the internal buffer. The DataEncoder object can also be constructed with
/// just a byte order and address size and data can be appended to the
/// internally owned buffer.
///
/// Clients can get a shared pointer to the data buffer when done modifying or
/// creating the data to keep the data around after the lifetime of a
/// DataEncoder object. \see GetDataBuffer
///
/// Client can get a reference to the object owned data as an array by calling
/// the GetData method. \see GetData
class DataEncoder {
public:
  /// Default constructor.
  ///
  /// Initialize all members to a default empty state and create a empty memory
  /// buffer that can be appended to. The ByteOrder and address size will be set
  /// to match the current host system.
  DataEncoder();

  /// Construct an encoder that copies the specified data into the object owned
  /// data buffer.
  ///
  /// This constructor is designed to be used when you have a data buffer and
  /// want to modify values within the buffer. A copy of the data will be made
  /// in the internally owned buffer and that data can be fixed up and appended
  /// to.
  ///
  /// \param[in] data
  ///     A pointer to caller owned data.
  ///
  /// \param[in] data_length
  ///     The length in bytes of \a data.
  ///
  /// \param[in] byte_order
  ///     A byte order for the data that will be encoded.
  ///
  /// \param[in] addr_size
  ///     A size of an address in bytes. \see PutAddress, AppendAddress
  DataEncoder(const void *data, uint32_t data_length,
              lldb::ByteOrder byte_order, uint8_t addr_size);

  /// Construct an encoder that owns a heap based memory buffer.
  ///
  /// This allows clients to create binary data from scratch by appending values
  /// with the methods that start with "Append".
  ///
  /// \param[in] byte_order
  ///     A byte order for the data that will be encoded.
  ///
  /// \param[in] addr_size
  ///     A size of an address in bytes. \see PutAddress, AppendAddress
  DataEncoder(lldb::ByteOrder byte_order, uint8_t addr_size);

  ~DataEncoder();

  /// Encode an unsigned integer of size \a byte_size to \a offset.
  ///
  /// Encode a single integer value at \a offset and return the offset that
  /// follows the newly encoded integer when the data is successfully encoded
  /// into the existing data. There must be enough room in the existing data,
  /// else UINT32_MAX will be returned to indicate that encoding failed.
  ///
  /// \param[in] offset
  ///     The offset within the contained data at which to put the encoded
  ///     integer.
  ///
  /// \param[in] byte_size
  ///     The size in byte of the integer to encode.
  ///
  /// \param[in] value
  ///     The integer value to write. The least significant bytes of
  ///     the integer value will be written if the size is less than
  ///     8 bytes.
  ///
  /// \return
  ///     The next offset in the bytes of this data if the integer
  ///     was successfully encoded, UINT32_MAX if the encoding failed.
  uint32_t PutUnsigned(uint32_t offset, uint32_t byte_size, uint64_t value);

  /// Encode an unsigned integer at offset \a offset.
  ///
  /// Encode a single unsigned integer value at \a offset and return the offset
  /// that follows the newly encoded integer when the data is successfully
  /// encoded into the existing data. There must be enough room in the data,
  /// else UINT32_MAX will be returned to indicate that encoding failed.
  ///
  /// \param[in] offset
  ///     The offset within the contained data at which to put the encoded
  ///     integer.
  ///
  /// \param[in] value
  ///     The integer value to write.
  ///
  /// \return
  ///     The next offset in the bytes of this data if the integer was
  ///     successfully encoded, UINT32_MAX if the encoding failed.
  uint32_t PutU8(uint32_t offset, uint8_t value);
  uint32_t PutU16(uint32_t offset, uint16_t value);
  uint32_t PutU32(uint32_t offset, uint32_t value);
  uint32_t PutU64(uint32_t offset, uint64_t value);

  /// Append a unsigned integer to the end of the owned data.
  ///
  /// \param value
  ///   A unsigned integer value to append.
  void AppendU8(uint8_t value);
  void AppendU16(uint16_t value);
  void AppendU32(uint32_t value);
  void AppendU64(uint64_t value);

  /// Append an address sized integer to the end of the owned data.
  ///
  /// \param addr
  ///    A unsigned integer address value to append. The size of the address
  ///    will be determined by the address size specified in the constructor.
  void AppendAddress(lldb::addr_t addr);

  /// Append a bytes to the end of the owned data.
  ///
  /// Append the bytes contained in the string reference. This function will
  /// not append a NULL termination character for a C string. Use the
  /// AppendCString function for this purpose.
  ///
  /// \param data
  ///     A string reference that contains bytes to append.
  void AppendData(llvm::StringRef data);

  /// Append a bytes to the end of the owned data.
  ///
  /// Append the bytes contained in the array reference.
  ///
  /// \param data
  ///     A array reference that contains bytes to append.
  void AppendData(llvm::ArrayRef<uint8_t> data);

  /// Append a C string to the end of the owned data.
  ///
  /// Append the bytes contained in the string reference along with an extra
  /// NULL termination character if the StringRef bytes doesn't include one as
  /// the last byte.
  ///
  /// \param data
  ///     A string reference that contains bytes to append.
  void AppendCString(llvm::StringRef data);

  /// Encode an arbitrary number of bytes.
  ///
  /// \param[in] offset
  ///     The offset in bytes into the contained data at which to
  ///     start encoding.
  ///
  /// \param[in] src
  ///     The buffer that contains the bytes to encode.
  ///
  /// \param[in] src_len
  ///     The number of bytes to encode.
  ///
  /// \return
  ///     The next valid offset within data if the put operation
  ///     was successful, else UINT32_MAX to indicate the put failed.
  uint32_t PutData(uint32_t offset, const void *src, uint32_t src_len);

  /// Encode an address in the existing buffer at \a offset bytes into the
  /// buffer.
  ///
  /// Encode a single address to the data and return the next offset where
  /// subsequent data would go. The size of the address comes from the \a
  /// m_addr_size member variable and should be set correctly prior to encoding
  /// any address values.
  ///
  /// \param[in] offset
  ///     The offset where to encode the address.
  ///
  /// \param[in] addr
  ///     The address to encode.
  ///
  /// \return
  ///     The next valid offset within data if the put operation
  ///     was successful, else UINT32_MAX to indicate the put failed.
  uint32_t PutAddress(uint32_t offset, lldb::addr_t addr);

  /// Put a C string to \a offset.
  ///
  /// Encodes a C string into the existing data including the terminating. If
  /// there is not enough room in the buffer to fit the entire C string and the
  /// NULL terminator in the existing buffer bounds, then this function will
  /// fail.
  ///
  /// \param[in] offset
  ///     The offset where to encode the string.
  ///
  /// \param[in] cstr
  ///     The string to encode.
  ///
  /// \return
  ///     The next valid offset within data if the put operation was successful,
  ///     else UINT32_MAX to indicate the put failed.
  uint32_t PutCString(uint32_t offset, const char *cstr);

  /// Get a shared copy of the heap based memory buffer owned by this object.
  ///
  /// This allows a data encoder to be used to create a data buffer that can
  /// be extracted and used elsewhere after this object is destroyed.
  ///
  /// \return
  ///     A shared pointer to the DataBufferHeap that contains the data that was
  ///     encoded into this object.
  std::shared_ptr<lldb_private::DataBufferHeap> GetDataBuffer() {
    return m_data_sp;
  }

  /// Get a access to the bytes that this references.
  ///
  /// This value will always return the data that this object references even if
  /// the object was constructed with caller owned data.
  ///
  /// \return
  ///     A array reference to the data that this object references.
  llvm::ArrayRef<uint8_t> GetData() const;

  /// Get the number of bytes contained in this object.
  ///
  /// \return
  ///     The total number of bytes of data this object refers to.
  size_t GetByteSize() const;

  lldb::ByteOrder GetByteOrder() const { return m_byte_order; }

  /// The address size to use when encoding pointers or addresses.
  uint8_t GetAddressByteSize() const { return m_addr_size; }

private:
  uint32_t BytesLeft(uint32_t offset) const {
    const uint32_t size = GetByteSize();
    if (size > offset)
      return size - offset;
    return 0;
  }

  /// Test the availability of \a length bytes of data from \a offset.
  ///
  /// \return
  ///     \b true if \a offset is a valid offset and there are \a
  ///     length bytes available at that offset, \b false otherwise.
  bool ValidOffsetForDataOfSize(uint32_t offset, uint32_t length) const {
    return length <= BytesLeft(offset);
  }

  /// Test the validity of \a offset.
  ///
  /// \return
  ///     \b true if \a offset is a valid offset into the data in this
  ///     object, \b false otherwise.
  bool ValidOffset(uint32_t offset) const { return offset < GetByteSize(); }

  /// The shared pointer to data that can grow as data is added
  std::shared_ptr<lldb_private::DataBufferHeap> m_data_sp;

  /// The byte order of the data we are encoding to.
  const lldb::ByteOrder m_byte_order;

  /// The address size to use when encoding pointers or addresses.
  const uint8_t m_addr_size;

  DataEncoder(const DataEncoder &) = delete;
  const DataEncoder &operator=(const DataEncoder &) = delete;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_DATAENCODER_H
