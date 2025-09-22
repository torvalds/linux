//===-- DataEncoder.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/DataEncoder.h"

#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"

#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"

#include <cstddef>

#include <cstring>

using namespace lldb;
using namespace lldb_private;
using namespace llvm::support::endian;

DataEncoder::DataEncoder()
    : m_data_sp(new DataBufferHeap()), m_byte_order(endian::InlHostByteOrder()),
      m_addr_size(sizeof(void *)) {}

DataEncoder::DataEncoder(const void *data, uint32_t length, ByteOrder endian,
                         uint8_t addr_size)
    : m_data_sp(new DataBufferHeap(data, length)), m_byte_order(endian),
      m_addr_size(addr_size) {}

DataEncoder::DataEncoder(ByteOrder endian, uint8_t addr_size)
    : m_data_sp(new DataBufferHeap()), m_byte_order(endian),
      m_addr_size(addr_size) {}

DataEncoder::~DataEncoder() = default;

llvm::ArrayRef<uint8_t> DataEncoder::GetData() const {
  return llvm::ArrayRef<uint8_t>(m_data_sp->GetBytes(), GetByteSize());
}

size_t DataEncoder::GetByteSize() const { return m_data_sp->GetByteSize(); }

// Extract a single unsigned char from the binary data and update the offset
// pointed to by "offset_ptr".
//
// RETURNS the byte that was extracted, or zero on failure.
uint32_t DataEncoder::PutU8(uint32_t offset, uint8_t value) {
  if (ValidOffset(offset)) {
    m_data_sp->GetBytes()[offset] = value;
    return offset + 1;
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutU16(uint32_t offset, uint16_t value) {
  if (ValidOffsetForDataOfSize(offset, sizeof(value))) {
    if (m_byte_order != endian::InlHostByteOrder())
      write16be(m_data_sp->GetBytes() + offset, value);
    else
      write16le(m_data_sp->GetBytes() + offset, value);

    return offset + sizeof(value);
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutU32(uint32_t offset, uint32_t value) {
  if (ValidOffsetForDataOfSize(offset, sizeof(value))) {
    if (m_byte_order != endian::InlHostByteOrder())
      write32be(m_data_sp->GetBytes() + offset, value);
    else
      write32le(m_data_sp->GetBytes() + offset, value);

    return offset + sizeof(value);
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutU64(uint32_t offset, uint64_t value) {
  if (ValidOffsetForDataOfSize(offset, sizeof(value))) {
    if (m_byte_order != endian::InlHostByteOrder())
      write64be(m_data_sp->GetBytes() + offset, value);
    else
      write64le(m_data_sp->GetBytes() + offset, value);

    return offset + sizeof(value);
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutUnsigned(uint32_t offset, uint32_t byte_size,
                                  uint64_t value) {
  switch (byte_size) {
  case 1:
    return PutU8(offset, value);
  case 2:
    return PutU16(offset, value);
  case 4:
    return PutU32(offset, value);
  case 8:
    return PutU64(offset, value);
  default:
    llvm_unreachable("GetMax64 unhandled case!");
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutData(uint32_t offset, const void *src,
                              uint32_t src_len) {
  if (src == nullptr || src_len == 0)
    return offset;

  if (ValidOffsetForDataOfSize(offset, src_len)) {
    memcpy(m_data_sp->GetBytes() + offset, src, src_len);
    return offset + src_len;
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutAddress(uint32_t offset, lldb::addr_t addr) {
  return PutUnsigned(offset, m_addr_size, addr);
}

uint32_t DataEncoder::PutCString(uint32_t offset, const char *cstr) {
  if (cstr != nullptr)
    return PutData(offset, cstr, strlen(cstr) + 1);
  return UINT32_MAX;
}

void DataEncoder::AppendU8(uint8_t value) {
  m_data_sp->AppendData(&value, sizeof(value));
}

void DataEncoder::AppendU16(uint16_t value) {
  uint32_t offset = m_data_sp->GetByteSize();
  m_data_sp->SetByteSize(m_data_sp->GetByteSize() + sizeof(value));
  PutU16(offset, value);
}

void DataEncoder::AppendU32(uint32_t value) {
  uint32_t offset = m_data_sp->GetByteSize();
  m_data_sp->SetByteSize(m_data_sp->GetByteSize() + sizeof(value));
  PutU32(offset, value);
}

void DataEncoder::AppendU64(uint64_t value) {
  uint32_t offset = m_data_sp->GetByteSize();
  m_data_sp->SetByteSize(m_data_sp->GetByteSize() + sizeof(value));
  PutU64(offset, value);
}

void DataEncoder::AppendAddress(lldb::addr_t addr) {
  switch (m_addr_size) {
  case 4:
    AppendU32(addr);
    break;
  case 8:
    AppendU64(addr);
    break;
  default:
    llvm_unreachable("AppendAddress unhandled case!");
  }
}

void DataEncoder::AppendData(llvm::StringRef data) {
  const char *bytes = data.data();
  const size_t length = data.size();
  if (bytes && length > 0)
    m_data_sp->AppendData(bytes, length);
}

void DataEncoder::AppendData(llvm::ArrayRef<uint8_t> data) {
  const uint8_t *bytes = data.data();
  const size_t length = data.size();
  if (bytes && length > 0)
    m_data_sp->AppendData(bytes, length);
}

void DataEncoder::AppendCString(llvm::StringRef data) {
  const char *bytes = data.data();
  const size_t length = data.size();
  if (bytes) {
    if (length > 0)
      m_data_sp->AppendData(bytes, length);
    if (length == 0 || bytes[length - 1] != '\0')
      AppendU8(0);
  }
}
