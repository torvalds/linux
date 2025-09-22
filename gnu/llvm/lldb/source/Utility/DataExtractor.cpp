//===-- DataExtractor.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/DataExtractor.h"

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/UUID.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <string>

#include <cctype>
#include <cinttypes>
#include <cstring>

using namespace lldb;
using namespace lldb_private;

static inline uint16_t ReadInt16(const unsigned char *ptr, offset_t offset) {
  uint16_t value;
  memcpy(&value, ptr + offset, 2);
  return value;
}

static inline uint32_t ReadInt32(const unsigned char *ptr,
                                 offset_t offset = 0) {
  uint32_t value;
  memcpy(&value, ptr + offset, 4);
  return value;
}

static inline uint64_t ReadInt64(const unsigned char *ptr,
                                 offset_t offset = 0) {
  uint64_t value;
  memcpy(&value, ptr + offset, 8);
  return value;
}

static inline uint16_t ReadInt16(const void *ptr) {
  uint16_t value;
  memcpy(&value, ptr, 2);
  return value;
}

static inline uint16_t ReadSwapInt16(const unsigned char *ptr,
                                     offset_t offset) {
  uint16_t value;
  memcpy(&value, ptr + offset, 2);
  return llvm::byteswap<uint16_t>(value);
}

static inline uint32_t ReadSwapInt32(const unsigned char *ptr,
                                     offset_t offset) {
  uint32_t value;
  memcpy(&value, ptr + offset, 4);
  return llvm::byteswap<uint32_t>(value);
}

static inline uint64_t ReadSwapInt64(const unsigned char *ptr,
                                     offset_t offset) {
  uint64_t value;
  memcpy(&value, ptr + offset, 8);
  return llvm::byteswap<uint64_t>(value);
}

static inline uint16_t ReadSwapInt16(const void *ptr) {
  uint16_t value;
  memcpy(&value, ptr, 2);
  return llvm::byteswap<uint16_t>(value);
}

static inline uint32_t ReadSwapInt32(const void *ptr) {
  uint32_t value;
  memcpy(&value, ptr, 4);
  return llvm::byteswap<uint32_t>(value);
}

static inline uint64_t ReadSwapInt64(const void *ptr) {
  uint64_t value;
  memcpy(&value, ptr, 8);
  return llvm::byteswap<uint64_t>(value);
}

static inline uint64_t ReadMaxInt64(const uint8_t *data, size_t byte_size,
                                    ByteOrder byte_order) {
  uint64_t res = 0;
  if (byte_order == eByteOrderBig)
    for (size_t i = 0; i < byte_size; ++i)
      res = (res << 8) | data[i];
  else {
    assert(byte_order == eByteOrderLittle);
    for (size_t i = 0; i < byte_size; ++i)
      res = (res << 8) | data[byte_size - 1 - i];
  }
  return res;
}

DataExtractor::DataExtractor()
    : m_byte_order(endian::InlHostByteOrder()), m_addr_size(sizeof(void *)),
      m_data_sp() {}

// This constructor allows us to use data that is owned by someone else. The
// data must stay around as long as this object is valid.
DataExtractor::DataExtractor(const void *data, offset_t length,
                             ByteOrder endian, uint32_t addr_size,
                             uint32_t target_byte_size /*=1*/)
    : m_start(const_cast<uint8_t *>(static_cast<const uint8_t *>(data))),
      m_end(const_cast<uint8_t *>(static_cast<const uint8_t *>(data)) + length),
      m_byte_order(endian), m_addr_size(addr_size), m_data_sp(),
      m_target_byte_size(target_byte_size) {
  assert(addr_size >= 1 && addr_size <= 8);
}

// Make a shared pointer reference to the shared data in "data_sp" and set the
// endian swapping setting to "swap", and the address size to "addr_size". The
// shared data reference will ensure the data lives as long as any
// DataExtractor objects exist that have a reference to this data.
DataExtractor::DataExtractor(const DataBufferSP &data_sp, ByteOrder endian,
                             uint32_t addr_size,
                             uint32_t target_byte_size /*=1*/)
    : m_byte_order(endian), m_addr_size(addr_size), m_data_sp(),
      m_target_byte_size(target_byte_size) {
  assert(addr_size >= 1 && addr_size <= 8);
  SetData(data_sp);
}

// Initialize this object with a subset of the data bytes in "data". If "data"
// contains shared data, then a reference to this shared data will added and
// the shared data will stay around as long as any object contains a reference
// to that data. The endian swap and address size settings are copied from
// "data".
DataExtractor::DataExtractor(const DataExtractor &data, offset_t offset,
                             offset_t length, uint32_t target_byte_size /*=1*/)
    : m_byte_order(data.m_byte_order), m_addr_size(data.m_addr_size),
      m_data_sp(), m_target_byte_size(target_byte_size) {
  assert(m_addr_size >= 1 && m_addr_size <= 8);
  if (data.ValidOffset(offset)) {
    offset_t bytes_available = data.GetByteSize() - offset;
    if (length > bytes_available)
      length = bytes_available;
    SetData(data, offset, length);
  }
}

DataExtractor::DataExtractor(const DataExtractor &rhs)
    : m_start(rhs.m_start), m_end(rhs.m_end), m_byte_order(rhs.m_byte_order),
      m_addr_size(rhs.m_addr_size), m_data_sp(rhs.m_data_sp),
      m_target_byte_size(rhs.m_target_byte_size) {
  assert(m_addr_size >= 1 && m_addr_size <= 8);
}

// Assignment operator
const DataExtractor &DataExtractor::operator=(const DataExtractor &rhs) {
  if (this != &rhs) {
    m_start = rhs.m_start;
    m_end = rhs.m_end;
    m_byte_order = rhs.m_byte_order;
    m_addr_size = rhs.m_addr_size;
    m_data_sp = rhs.m_data_sp;
  }
  return *this;
}

DataExtractor::~DataExtractor() = default;

// Clears the object contents back to a default invalid state, and release any
// references to shared data that this object may contain.
void DataExtractor::Clear() {
  m_start = nullptr;
  m_end = nullptr;
  m_byte_order = endian::InlHostByteOrder();
  m_addr_size = sizeof(void *);
  m_data_sp.reset();
}

// If this object contains shared data, this function returns the offset into
// that shared data. Else zero is returned.
size_t DataExtractor::GetSharedDataOffset() const {
  if (m_start != nullptr) {
    const DataBuffer *data = m_data_sp.get();
    if (data != nullptr) {
      const uint8_t *data_bytes = data->GetBytes();
      if (data_bytes != nullptr) {
        assert(m_start >= data_bytes);
        return m_start - data_bytes;
      }
    }
  }
  return 0;
}

// Set the data with which this object will extract from to data starting at
// BYTES and set the length of the data to LENGTH bytes long. The data is
// externally owned must be around at least as long as this object points to
// the data. No copy of the data is made, this object just refers to this data
// and can extract from it. If this object refers to any shared data upon
// entry, the reference to that data will be released. Is SWAP is set to true,
// any data extracted will be endian swapped.
lldb::offset_t DataExtractor::SetData(const void *bytes, offset_t length,
                                      ByteOrder endian) {
  m_byte_order = endian;
  m_data_sp.reset();
  if (bytes == nullptr || length == 0) {
    m_start = nullptr;
    m_end = nullptr;
  } else {
    m_start = const_cast<uint8_t *>(static_cast<const uint8_t *>(bytes));
    m_end = m_start + length;
  }
  return GetByteSize();
}

// Assign the data for this object to be a subrange in "data" starting
// "data_offset" bytes into "data" and ending "data_length" bytes later. If
// "data_offset" is not a valid offset into "data", then this object will
// contain no bytes. If "data_offset" is within "data" yet "data_length" is too
// large, the length will be capped at the number of bytes remaining in "data".
// If "data" contains a shared pointer to other data, then a ref counted
// pointer to that data will be made in this object. If "data" doesn't contain
// a shared pointer to data, then the bytes referred to in "data" will need to
// exist at least as long as this object refers to those bytes. The address
// size and endian swap settings are copied from the current values in "data".
lldb::offset_t DataExtractor::SetData(const DataExtractor &data,
                                      offset_t data_offset,
                                      offset_t data_length) {
  m_addr_size = data.m_addr_size;
  assert(m_addr_size >= 1 && m_addr_size <= 8);
  // If "data" contains shared pointer to data, then we can use that
  if (data.m_data_sp) {
    m_byte_order = data.m_byte_order;
    return SetData(data.m_data_sp, data.GetSharedDataOffset() + data_offset,
                   data_length);
  }

  // We have a DataExtractor object that just has a pointer to bytes
  if (data.ValidOffset(data_offset)) {
    if (data_length > data.GetByteSize() - data_offset)
      data_length = data.GetByteSize() - data_offset;
    return SetData(data.GetDataStart() + data_offset, data_length,
                   data.GetByteOrder());
  }
  return 0;
}

// Assign the data for this object to be a subrange of the shared data in
// "data_sp" starting "data_offset" bytes into "data_sp" and ending
// "data_length" bytes later. If "data_offset" is not a valid offset into
// "data_sp", then this object will contain no bytes. If "data_offset" is
// within "data_sp" yet "data_length" is too large, the length will be capped
// at the number of bytes remaining in "data_sp". A ref counted pointer to the
// data in "data_sp" will be made in this object IF the number of bytes this
// object refers to in greater than zero (if at least one byte was available
// starting at "data_offset") to ensure the data stays around as long as it is
// needed. The address size and endian swap settings will remain unchanged from
// their current settings.
lldb::offset_t DataExtractor::SetData(const DataBufferSP &data_sp,
                                      offset_t data_offset,
                                      offset_t data_length) {
  m_start = m_end = nullptr;

  if (data_length > 0) {
    m_data_sp = data_sp;
    if (data_sp) {
      const size_t data_size = data_sp->GetByteSize();
      if (data_offset < data_size) {
        m_start = data_sp->GetBytes() + data_offset;
        const size_t bytes_left = data_size - data_offset;
        // Cap the length of we asked for too many
        if (data_length <= bytes_left)
          m_end = m_start + data_length; // We got all the bytes we wanted
        else
          m_end = m_start + bytes_left; // Not all the bytes requested were
                                        // available in the shared data
      }
    }
  }

  size_t new_size = GetByteSize();

  // Don't hold a shared pointer to the data buffer if we don't share any valid
  // bytes in the shared buffer.
  if (new_size == 0)
    m_data_sp.reset();

  return new_size;
}

// Extract a single unsigned char from the binary data and update the offset
// pointed to by "offset_ptr".
//
// RETURNS the byte that was extracted, or zero on failure.
uint8_t DataExtractor::GetU8(offset_t *offset_ptr) const {
  const uint8_t *data = static_cast<const uint8_t *>(GetData(offset_ptr, 1));
  if (data)
    return *data;
  return 0;
}

// Extract "count" unsigned chars from the binary data and update the offset
// pointed to by "offset_ptr". The extracted data is copied into "dst".
//
// RETURNS the non-nullptr buffer pointer upon successful extraction of
// all the requested bytes, or nullptr when the data is not available in the
// buffer due to being out of bounds, or insufficient data.
void *DataExtractor::GetU8(offset_t *offset_ptr, void *dst,
                           uint32_t count) const {
  const uint8_t *data =
      static_cast<const uint8_t *>(GetData(offset_ptr, count));
  if (data) {
    // Copy the data into the buffer
    memcpy(dst, data, count);
    // Return a non-nullptr pointer to the converted data as an indicator of
    // success
    return dst;
  }
  return nullptr;
}

// Extract a single uint16_t from the data and update the offset pointed to by
// "offset_ptr".
//
// RETURNS the uint16_t that was extracted, or zero on failure.
uint16_t DataExtractor::GetU16(offset_t *offset_ptr) const {
  uint16_t val = 0;
  const uint8_t *data =
      static_cast<const uint8_t *>(GetData(offset_ptr, sizeof(val)));
  if (data) {
    if (m_byte_order != endian::InlHostByteOrder())
      val = ReadSwapInt16(data);
    else
      val = ReadInt16(data);
  }
  return val;
}

uint16_t DataExtractor::GetU16_unchecked(offset_t *offset_ptr) const {
  uint16_t val;
  if (m_byte_order == endian::InlHostByteOrder())
    val = ReadInt16(m_start, *offset_ptr);
  else
    val = ReadSwapInt16(m_start, *offset_ptr);
  *offset_ptr += sizeof(val);
  return val;
}

uint32_t DataExtractor::GetU32_unchecked(offset_t *offset_ptr) const {
  uint32_t val;
  if (m_byte_order == endian::InlHostByteOrder())
    val = ReadInt32(m_start, *offset_ptr);
  else
    val = ReadSwapInt32(m_start, *offset_ptr);
  *offset_ptr += sizeof(val);
  return val;
}

uint64_t DataExtractor::GetU64_unchecked(offset_t *offset_ptr) const {
  uint64_t val;
  if (m_byte_order == endian::InlHostByteOrder())
    val = ReadInt64(m_start, *offset_ptr);
  else
    val = ReadSwapInt64(m_start, *offset_ptr);
  *offset_ptr += sizeof(val);
  return val;
}

// Extract "count" uint16_t values from the binary data and update the offset
// pointed to by "offset_ptr". The extracted data is copied into "dst".
//
// RETURNS the non-nullptr buffer pointer upon successful extraction of
// all the requested bytes, or nullptr when the data is not available in the
// buffer due to being out of bounds, or insufficient data.
void *DataExtractor::GetU16(offset_t *offset_ptr, void *void_dst,
                            uint32_t count) const {
  const size_t src_size = sizeof(uint16_t) * count;
  const uint16_t *src =
      static_cast<const uint16_t *>(GetData(offset_ptr, src_size));
  if (src) {
    if (m_byte_order != endian::InlHostByteOrder()) {
      uint16_t *dst_pos = static_cast<uint16_t *>(void_dst);
      uint16_t *dst_end = dst_pos + count;
      const uint16_t *src_pos = src;
      while (dst_pos < dst_end) {
        *dst_pos = ReadSwapInt16(src_pos);
        ++dst_pos;
        ++src_pos;
      }
    } else {
      memcpy(void_dst, src, src_size);
    }
    // Return a non-nullptr pointer to the converted data as an indicator of
    // success
    return void_dst;
  }
  return nullptr;
}

// Extract a single uint32_t from the data and update the offset pointed to by
// "offset_ptr".
//
// RETURNS the uint32_t that was extracted, or zero on failure.
uint32_t DataExtractor::GetU32(offset_t *offset_ptr) const {
  uint32_t val = 0;
  const uint8_t *data =
      static_cast<const uint8_t *>(GetData(offset_ptr, sizeof(val)));
  if (data) {
    if (m_byte_order != endian::InlHostByteOrder()) {
      val = ReadSwapInt32(data);
    } else {
      memcpy(&val, data, 4);
    }
  }
  return val;
}

// Extract "count" uint32_t values from the binary data and update the offset
// pointed to by "offset_ptr". The extracted data is copied into "dst".
//
// RETURNS the non-nullptr buffer pointer upon successful extraction of
// all the requested bytes, or nullptr when the data is not available in the
// buffer due to being out of bounds, or insufficient data.
void *DataExtractor::GetU32(offset_t *offset_ptr, void *void_dst,
                            uint32_t count) const {
  const size_t src_size = sizeof(uint32_t) * count;
  const uint32_t *src =
      static_cast<const uint32_t *>(GetData(offset_ptr, src_size));
  if (src) {
    if (m_byte_order != endian::InlHostByteOrder()) {
      uint32_t *dst_pos = static_cast<uint32_t *>(void_dst);
      uint32_t *dst_end = dst_pos + count;
      const uint32_t *src_pos = src;
      while (dst_pos < dst_end) {
        *dst_pos = ReadSwapInt32(src_pos);
        ++dst_pos;
        ++src_pos;
      }
    } else {
      memcpy(void_dst, src, src_size);
    }
    // Return a non-nullptr pointer to the converted data as an indicator of
    // success
    return void_dst;
  }
  return nullptr;
}

// Extract a single uint64_t from the data and update the offset pointed to by
// "offset_ptr".
//
// RETURNS the uint64_t that was extracted, or zero on failure.
uint64_t DataExtractor::GetU64(offset_t *offset_ptr) const {
  uint64_t val = 0;
  const uint8_t *data =
      static_cast<const uint8_t *>(GetData(offset_ptr, sizeof(val)));
  if (data) {
    if (m_byte_order != endian::InlHostByteOrder()) {
      val = ReadSwapInt64(data);
    } else {
      memcpy(&val, data, 8);
    }
  }
  return val;
}

// GetU64
//
// Get multiple consecutive 64 bit values. Return true if the entire read
// succeeds and increment the offset pointed to by offset_ptr, else return
// false and leave the offset pointed to by offset_ptr unchanged.
void *DataExtractor::GetU64(offset_t *offset_ptr, void *void_dst,
                            uint32_t count) const {
  const size_t src_size = sizeof(uint64_t) * count;
  const uint64_t *src =
      static_cast<const uint64_t *>(GetData(offset_ptr, src_size));
  if (src) {
    if (m_byte_order != endian::InlHostByteOrder()) {
      uint64_t *dst_pos = static_cast<uint64_t *>(void_dst);
      uint64_t *dst_end = dst_pos + count;
      const uint64_t *src_pos = src;
      while (dst_pos < dst_end) {
        *dst_pos = ReadSwapInt64(src_pos);
        ++dst_pos;
        ++src_pos;
      }
    } else {
      memcpy(void_dst, src, src_size);
    }
    // Return a non-nullptr pointer to the converted data as an indicator of
    // success
    return void_dst;
  }
  return nullptr;
}

uint32_t DataExtractor::GetMaxU32(offset_t *offset_ptr,
                                  size_t byte_size) const {
  lldbassert(byte_size > 0 && byte_size <= 4 && "GetMaxU32 invalid byte_size!");
  return GetMaxU64(offset_ptr, byte_size);
}

uint64_t DataExtractor::GetMaxU64(offset_t *offset_ptr,
                                  size_t byte_size) const {
  lldbassert(byte_size > 0 && byte_size <= 8 && "GetMaxU64 invalid byte_size!");
  switch (byte_size) {
  case 1:
    return GetU8(offset_ptr);
  case 2:
    return GetU16(offset_ptr);
  case 4:
    return GetU32(offset_ptr);
  case 8:
    return GetU64(offset_ptr);
  default: {
    // General case.
    const uint8_t *data =
        static_cast<const uint8_t *>(GetData(offset_ptr, byte_size));
    if (data == nullptr)
      return 0;
    return ReadMaxInt64(data, byte_size, m_byte_order);
  }
  }
  return 0;
}

uint64_t DataExtractor::GetMaxU64_unchecked(offset_t *offset_ptr,
                                            size_t byte_size) const {
  switch (byte_size) {
  case 1:
    return GetU8_unchecked(offset_ptr);
  case 2:
    return GetU16_unchecked(offset_ptr);
  case 4:
    return GetU32_unchecked(offset_ptr);
  case 8:
    return GetU64_unchecked(offset_ptr);
  default: {
    uint64_t res = ReadMaxInt64(&m_start[*offset_ptr], byte_size, m_byte_order);
    *offset_ptr += byte_size;
    return res;
  }
  }
  return 0;
}

int64_t DataExtractor::GetMaxS64(offset_t *offset_ptr, size_t byte_size) const {
  uint64_t u64 = GetMaxU64(offset_ptr, byte_size);
  return llvm::SignExtend64(u64, 8 * byte_size);
}

uint64_t DataExtractor::GetMaxU64Bitfield(offset_t *offset_ptr, size_t size,
                                          uint32_t bitfield_bit_size,
                                          uint32_t bitfield_bit_offset) const {
  assert(bitfield_bit_size <= 64);
  uint64_t uval64 = GetMaxU64(offset_ptr, size);

  if (bitfield_bit_size == 0)
    return uval64;

  int32_t lsbcount = bitfield_bit_offset;
  if (m_byte_order == eByteOrderBig)
    lsbcount = size * 8 - bitfield_bit_offset - bitfield_bit_size;

  if (lsbcount > 0)
    uval64 >>= lsbcount;

  uint64_t bitfield_mask =
      (bitfield_bit_size == 64
           ? std::numeric_limits<uint64_t>::max()
           : ((static_cast<uint64_t>(1) << bitfield_bit_size) - 1));
  if (!bitfield_mask && bitfield_bit_offset == 0 && bitfield_bit_size == 64)
    return uval64;

  uval64 &= bitfield_mask;

  return uval64;
}

int64_t DataExtractor::GetMaxS64Bitfield(offset_t *offset_ptr, size_t size,
                                         uint32_t bitfield_bit_size,
                                         uint32_t bitfield_bit_offset) const {
  assert(size >= 1 && "GetMaxS64Bitfield size must be >= 1");
  assert(size <= 8 && "GetMaxS64Bitfield size must be <= 8");
  int64_t sval64 = GetMaxS64(offset_ptr, size);
  if (bitfield_bit_size == 0)
    return sval64;
  int32_t lsbcount = bitfield_bit_offset;
  if (m_byte_order == eByteOrderBig)
    lsbcount = size * 8 - bitfield_bit_offset - bitfield_bit_size;
  if (lsbcount > 0)
    sval64 >>= lsbcount;
  uint64_t bitfield_mask = llvm::maskTrailingOnes<uint64_t>(bitfield_bit_size);
  sval64 &= bitfield_mask;
  // sign extend if needed
  if (sval64 & ((static_cast<uint64_t>(1)) << (bitfield_bit_size - 1)))
    sval64 |= ~bitfield_mask;
  return sval64;
}

float DataExtractor::GetFloat(offset_t *offset_ptr) const {
  return Get<float>(offset_ptr, 0.0f);
}

double DataExtractor::GetDouble(offset_t *offset_ptr) const {
  return Get<double>(offset_ptr, 0.0);
}

long double DataExtractor::GetLongDouble(offset_t *offset_ptr) const {
  long double val = 0.0;
#if defined(__i386__) || defined(__amd64__) || defined(__x86_64__) ||          \
    defined(_M_IX86) || defined(_M_IA64) || defined(_M_X64)
  *offset_ptr += CopyByteOrderedData(*offset_ptr, 10, &val, sizeof(val),
                                     endian::InlHostByteOrder());
#else
  *offset_ptr += CopyByteOrderedData(*offset_ptr, sizeof(val), &val,
                                     sizeof(val), endian::InlHostByteOrder());
#endif
  return val;
}

// Extract a single address from the data and update the offset pointed to by
// "offset_ptr". The size of the extracted address comes from the
// "this->m_addr_size" member variable and should be set correctly prior to
// extracting any address values.
//
// RETURNS the address that was extracted, or zero on failure.
uint64_t DataExtractor::GetAddress(offset_t *offset_ptr) const {
  assert(m_addr_size >= 1 && m_addr_size <= 8);
  return GetMaxU64(offset_ptr, m_addr_size);
}

uint64_t DataExtractor::GetAddress_unchecked(offset_t *offset_ptr) const {
  assert(m_addr_size >= 1 && m_addr_size <= 8);
  return GetMaxU64_unchecked(offset_ptr, m_addr_size);
}

size_t DataExtractor::ExtractBytes(offset_t offset, offset_t length,
                                   ByteOrder dst_byte_order, void *dst) const {
  const uint8_t *src = PeekData(offset, length);
  if (src) {
    if (dst_byte_order != GetByteOrder()) {
      // Validate that only a word- or register-sized dst is byte swapped
      assert(length == 1 || length == 2 || length == 4 || length == 8 ||
             length == 10 || length == 16 || length == 32);

      for (uint32_t i = 0; i < length; ++i)
        (static_cast<uint8_t *>(dst))[i] = src[length - i - 1];
    } else
      ::memcpy(dst, src, length);
    return length;
  }
  return 0;
}

// Extract data as it exists in target memory
lldb::offset_t DataExtractor::CopyData(offset_t offset, offset_t length,
                                       void *dst) const {
  const uint8_t *src = PeekData(offset, length);
  if (src) {
    ::memcpy(dst, src, length);
    return length;
  }
  return 0;
}

// Extract data and swap if needed when doing the copy
lldb::offset_t
DataExtractor::CopyByteOrderedData(offset_t src_offset, offset_t src_len,
                                   void *dst_void_ptr, offset_t dst_len,
                                   ByteOrder dst_byte_order) const {
  // Validate the source info
  if (!ValidOffsetForDataOfSize(src_offset, src_len))
    assert(ValidOffsetForDataOfSize(src_offset, src_len));
  assert(src_len > 0);
  assert(m_byte_order == eByteOrderBig || m_byte_order == eByteOrderLittle);

  // Validate the destination info
  assert(dst_void_ptr != nullptr);
  assert(dst_len > 0);
  assert(dst_byte_order == eByteOrderBig || dst_byte_order == eByteOrderLittle);

  // Validate that only a word- or register-sized dst is byte swapped
  assert(dst_byte_order == m_byte_order || dst_len == 1 || dst_len == 2 ||
         dst_len == 4 || dst_len == 8 || dst_len == 10 || dst_len == 16 ||
         dst_len == 32);

  // Must have valid byte orders set in this object and for destination
  if (!(dst_byte_order == eByteOrderBig ||
        dst_byte_order == eByteOrderLittle) ||
      !(m_byte_order == eByteOrderBig || m_byte_order == eByteOrderLittle))
    return 0;

  uint8_t *dst = static_cast<uint8_t *>(dst_void_ptr);
  const uint8_t *src = PeekData(src_offset, src_len);
  if (src) {
    if (dst_len >= src_len) {
      // We are copying the entire value from src into dst. Calculate how many,
      // if any, zeroes we need for the most significant bytes if "dst_len" is
      // greater than "src_len"...
      const size_t num_zeroes = dst_len - src_len;
      if (dst_byte_order == eByteOrderBig) {
        // Big endian, so we lead with zeroes...
        if (num_zeroes > 0)
          ::memset(dst, 0, num_zeroes);
        // Then either copy or swap the rest
        if (m_byte_order == eByteOrderBig) {
          ::memcpy(dst + num_zeroes, src, src_len);
        } else {
          for (uint32_t i = 0; i < src_len; ++i)
            dst[i + num_zeroes] = src[src_len - 1 - i];
        }
      } else {
        // Little endian destination, so we lead the value bytes
        if (m_byte_order == eByteOrderBig) {
          for (uint32_t i = 0; i < src_len; ++i)
            dst[i] = src[src_len - 1 - i];
        } else {
          ::memcpy(dst, src, src_len);
        }
        // And zero the rest...
        if (num_zeroes > 0)
          ::memset(dst + src_len, 0, num_zeroes);
      }
      return src_len;
    } else {
      // We are only copying some of the value from src into dst..

      if (dst_byte_order == eByteOrderBig) {
        // Big endian dst
        if (m_byte_order == eByteOrderBig) {
          // Big endian dst, with big endian src
          ::memcpy(dst, src + (src_len - dst_len), dst_len);
        } else {
          // Big endian dst, with little endian src
          for (uint32_t i = 0; i < dst_len; ++i)
            dst[i] = src[dst_len - 1 - i];
        }
      } else {
        // Little endian dst
        if (m_byte_order == eByteOrderBig) {
          // Little endian dst, with big endian src
          for (uint32_t i = 0; i < dst_len; ++i)
            dst[i] = src[src_len - 1 - i];
        } else {
          // Little endian dst, with big endian src
          ::memcpy(dst, src, dst_len);
        }
      }
      return dst_len;
    }
  }
  return 0;
}

// Extracts a variable length NULL terminated C string from the data at the
// offset pointed to by "offset_ptr".  The "offset_ptr" will be updated with
// the offset of the byte that follows the NULL terminator byte.
//
// If the offset pointed to by "offset_ptr" is out of bounds, or if "length" is
// non-zero and there aren't enough available bytes, nullptr will be returned
// and "offset_ptr" will not be updated.
const char *DataExtractor::GetCStr(offset_t *offset_ptr) const {
  const char *start = reinterpret_cast<const char *>(PeekData(*offset_ptr, 1));
  // Already at the end of the data.
  if (!start)
    return nullptr;

  const char *end = reinterpret_cast<const char *>(m_end);

  // Check all bytes for a null terminator that terminates a C string.
  const char *terminator_or_end = std::find(start, end, '\0');

  // We didn't find a null terminator, so return nullptr to indicate that there
  // is no valid C string at that offset.
  if (terminator_or_end == end)
    return nullptr;

  // Update offset_ptr for the caller to point to the data behind the
  // terminator (which is 1 byte long).
  *offset_ptr += (terminator_or_end - start + 1UL);
  return start;
}

// Extracts a NULL terminated C string from the fixed length field of length
// "len" at the offset pointed to by "offset_ptr". The "offset_ptr" will be
// updated with the offset of the byte that follows the fixed length field.
//
// If the offset pointed to by "offset_ptr" is out of bounds, or if the offset
// plus the length of the field is out of bounds, or if the field does not
// contain a NULL terminator byte, nullptr will be returned and "offset_ptr"
// will not be updated.
const char *DataExtractor::GetCStr(offset_t *offset_ptr, offset_t len) const {
  const char *cstr = reinterpret_cast<const char *>(PeekData(*offset_ptr, len));
  if (cstr != nullptr) {
    if (memchr(cstr, '\0', len) == nullptr) {
      return nullptr;
    }
    *offset_ptr += len;
    return cstr;
  }
  return nullptr;
}

// Peeks at a string in the contained data. No verification is done to make
// sure the entire string lies within the bounds of this object's data, only
// "offset" is verified to be a valid offset.
//
// Returns a valid C string pointer if "offset" is a valid offset in this
// object's data, else nullptr is returned.
const char *DataExtractor::PeekCStr(offset_t offset) const {
  return reinterpret_cast<const char *>(PeekData(offset, 1));
}

// Extracts an unsigned LEB128 number from this object's data starting at the
// offset pointed to by "offset_ptr". The offset pointed to by "offset_ptr"
// will be updated with the offset of the byte following the last extracted
// byte.
//
// Returned the extracted integer value.
uint64_t DataExtractor::GetULEB128(offset_t *offset_ptr) const {
  const uint8_t *src = PeekData(*offset_ptr, 1);
  if (src == nullptr)
    return 0;

  unsigned byte_count = 0;
  uint64_t result = llvm::decodeULEB128(src, &byte_count, m_end);
  *offset_ptr += byte_count;
  return result;
}

// Extracts an signed LEB128 number from this object's data starting at the
// offset pointed to by "offset_ptr". The offset pointed to by "offset_ptr"
// will be updated with the offset of the byte following the last extracted
// byte.
//
// Returned the extracted integer value.
int64_t DataExtractor::GetSLEB128(offset_t *offset_ptr) const {
  const uint8_t *src = PeekData(*offset_ptr, 1);
  if (src == nullptr)
    return 0;

  unsigned byte_count = 0;
  int64_t result = llvm::decodeSLEB128(src, &byte_count, m_end);
  *offset_ptr += byte_count;
  return result;
}

// Skips a ULEB128 number (signed or unsigned) from this object's data starting
// at the offset pointed to by "offset_ptr". The offset pointed to by
// "offset_ptr" will be updated with the offset of the byte following the last
// extracted byte.
//
// Returns the number of bytes consumed during the extraction.
uint32_t DataExtractor::Skip_LEB128(offset_t *offset_ptr) const {
  uint32_t bytes_consumed = 0;
  const uint8_t *src = PeekData(*offset_ptr, 1);
  if (src == nullptr)
    return 0;

  const uint8_t *end = m_end;

  if (src < end) {
    const uint8_t *src_pos = src;
    while ((src_pos < end) && (*src_pos++ & 0x80))
      ++bytes_consumed;
    *offset_ptr += src_pos - src;
  }
  return bytes_consumed;
}

// Dumps bytes from this object's data to the stream "s" starting
// "start_offset" bytes into this data, and ending with the byte before
// "end_offset". "base_addr" will be added to the offset into the dumped data
// when showing the offset into the data in the output information.
// "num_per_line" objects of type "type" will be dumped with the option to
// override the format for each object with "type_format". "type_format" is a
// printf style formatting string. If "type_format" is nullptr, then an
// appropriate format string will be used for the supplied "type". If the
// stream "s" is nullptr, then the output will be send to Log().
lldb::offset_t DataExtractor::PutToLog(Log *log, offset_t start_offset,
                                       offset_t length, uint64_t base_addr,
                                       uint32_t num_per_line,
                                       DataExtractor::Type type) const {
  if (log == nullptr)
    return start_offset;

  offset_t offset;
  offset_t end_offset;
  uint32_t count;
  StreamString sstr;
  for (offset = start_offset, end_offset = offset + length, count = 0;
       ValidOffset(offset) && offset < end_offset; ++count) {
    if ((count % num_per_line) == 0) {
      // Print out any previous string
      if (sstr.GetSize() > 0) {
        log->PutString(sstr.GetString());
        sstr.Clear();
      }
      // Reset string offset and fill the current line string with address:
      if (base_addr != LLDB_INVALID_ADDRESS)
        sstr.Printf("0x%8.8" PRIx64 ":",
                    static_cast<uint64_t>(base_addr + (offset - start_offset)));
    }

    switch (type) {
    case TypeUInt8:
      sstr.Printf(" %2.2x", GetU8(&offset));
      break;
    case TypeChar: {
      char ch = GetU8(&offset);
      sstr.Printf(" %c", llvm::isPrint(ch) ? ch : ' ');
    } break;
    case TypeUInt16:
      sstr.Printf(" %4.4x", GetU16(&offset));
      break;
    case TypeUInt32:
      sstr.Printf(" %8.8x", GetU32(&offset));
      break;
    case TypeUInt64:
      sstr.Printf(" %16.16" PRIx64, GetU64(&offset));
      break;
    case TypePointer:
      sstr.Printf(" 0x%" PRIx64, GetAddress(&offset));
      break;
    case TypeULEB128:
      sstr.Printf(" 0x%" PRIx64, GetULEB128(&offset));
      break;
    case TypeSLEB128:
      sstr.Printf(" %" PRId64, GetSLEB128(&offset));
      break;
    }
  }

  if (!sstr.Empty())
    log->PutString(sstr.GetString());

  return offset; // Return the offset at which we ended up
}

size_t DataExtractor::Copy(DataExtractor &dest_data) const {
  if (m_data_sp) {
    // we can pass along the SP to the data
    dest_data.SetData(m_data_sp);
  } else {
    const uint8_t *base_ptr = m_start;
    size_t data_size = GetByteSize();
    dest_data.SetData(DataBufferSP(new DataBufferHeap(base_ptr, data_size)));
  }
  return GetByteSize();
}

bool DataExtractor::Append(DataExtractor &rhs) {
  if (rhs.GetByteOrder() != GetByteOrder())
    return false;

  if (rhs.GetByteSize() == 0)
    return true;

  if (GetByteSize() == 0)
    return (rhs.Copy(*this) > 0);

  size_t bytes = GetByteSize() + rhs.GetByteSize();

  DataBufferHeap *buffer_heap_ptr = nullptr;
  DataBufferSP buffer_sp(buffer_heap_ptr = new DataBufferHeap(bytes, 0));

  if (!buffer_sp || buffer_heap_ptr == nullptr)
    return false;

  uint8_t *bytes_ptr = buffer_heap_ptr->GetBytes();

  memcpy(bytes_ptr, GetDataStart(), GetByteSize());
  memcpy(bytes_ptr + GetByteSize(), rhs.GetDataStart(), rhs.GetByteSize());

  SetData(buffer_sp);

  return true;
}

bool DataExtractor::Append(void *buf, offset_t length) {
  if (buf == nullptr)
    return false;

  if (length == 0)
    return true;

  size_t bytes = GetByteSize() + length;

  DataBufferHeap *buffer_heap_ptr = nullptr;
  DataBufferSP buffer_sp(buffer_heap_ptr = new DataBufferHeap(bytes, 0));

  if (!buffer_sp || buffer_heap_ptr == nullptr)
    return false;

  uint8_t *bytes_ptr = buffer_heap_ptr->GetBytes();

  if (GetByteSize() > 0)
    memcpy(bytes_ptr, GetDataStart(), GetByteSize());

  memcpy(bytes_ptr + GetByteSize(), buf, length);

  SetData(buffer_sp);

  return true;
}

void DataExtractor::Checksum(llvm::SmallVectorImpl<uint8_t> &dest,
                             uint64_t max_data) {
  if (max_data == 0)
    max_data = GetByteSize();
  else
    max_data = std::min(max_data, GetByteSize());

  llvm::MD5 md5;

  const llvm::ArrayRef<uint8_t> data(GetDataStart(), max_data);
  md5.update(data);

  llvm::MD5::MD5Result result;
  md5.final(result);

  dest.clear();
  dest.append(result.begin(), result.end());
}
