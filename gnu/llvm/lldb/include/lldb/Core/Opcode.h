//===-- Opcode.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_OPCODE_H
#define LLDB_CORE_OPCODE_H

#include "lldb/Utility/Endian.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/Support/SwapByteOrder.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace lldb {
class SBInstruction;
}

namespace lldb_private {
class DataExtractor;
class Stream;

class Opcode {
public:
  enum Type {
    eTypeInvalid,
    eType8,
    eType16,
    eType16_2, // a 32-bit Thumb instruction, made up of two words
    eType32,
    eType64,
    eTypeBytes
  };

  Opcode() = default;

  Opcode(uint8_t inst, lldb::ByteOrder order)
      : m_byte_order(order), m_type(eType8) {
    m_data.inst8 = inst;
  }

  Opcode(uint16_t inst, lldb::ByteOrder order)
      : m_byte_order(order), m_type(eType16) {
    m_data.inst16 = inst;
  }

  Opcode(uint32_t inst, lldb::ByteOrder order)
      : m_byte_order(order), m_type(eType32) {
    m_data.inst32 = inst;
  }

  Opcode(uint64_t inst, lldb::ByteOrder order)
      : m_byte_order(order), m_type(eType64) {
    m_data.inst64 = inst;
  }

  Opcode(uint8_t *bytes, size_t length)
      : m_byte_order(lldb::eByteOrderInvalid) {
    SetOpcodeBytes(bytes, length);
  }

  void Clear() {
    m_byte_order = lldb::eByteOrderInvalid;
    m_type = Opcode::eTypeInvalid;
  }

  Opcode::Type GetType() const { return m_type; }

  uint8_t GetOpcode8(uint8_t invalid_opcode = UINT8_MAX) const {
    switch (m_type) {
    case Opcode::eTypeInvalid:
      break;
    case Opcode::eType8:
      return m_data.inst8;
    case Opcode::eType16:
      break;
    case Opcode::eType16_2:
      break;
    case Opcode::eType32:
      break;
    case Opcode::eType64:
      break;
    case Opcode::eTypeBytes:
      break;
    }
    return invalid_opcode;
  }

  uint16_t GetOpcode16(uint16_t invalid_opcode = UINT16_MAX) const {
    switch (m_type) {
    case Opcode::eTypeInvalid:
      break;
    case Opcode::eType8:
      return m_data.inst8;
    case Opcode::eType16:
      return GetEndianSwap() ? llvm::byteswap<uint16_t>(m_data.inst16)
                             : m_data.inst16;
    case Opcode::eType16_2:
      break;
    case Opcode::eType32:
      break;
    case Opcode::eType64:
      break;
    case Opcode::eTypeBytes:
      break;
    }
    return invalid_opcode;
  }

  uint32_t GetOpcode32(uint32_t invalid_opcode = UINT32_MAX) const {
    switch (m_type) {
    case Opcode::eTypeInvalid:
      break;
    case Opcode::eType8:
      return m_data.inst8;
    case Opcode::eType16:
      return GetEndianSwap() ? llvm::byteswap<uint16_t>(m_data.inst16)
                             : m_data.inst16;
    case Opcode::eType16_2: // passthrough
    case Opcode::eType32:
      return GetEndianSwap() ? llvm::byteswap<uint32_t>(m_data.inst32)
                             : m_data.inst32;
    case Opcode::eType64:
      break;
    case Opcode::eTypeBytes:
      break;
    }
    return invalid_opcode;
  }

  uint64_t GetOpcode64(uint64_t invalid_opcode = UINT64_MAX) const {
    switch (m_type) {
    case Opcode::eTypeInvalid:
      break;
    case Opcode::eType8:
      return m_data.inst8;
    case Opcode::eType16:
      return GetEndianSwap() ? llvm::byteswap<uint16_t>(m_data.inst16)
                             : m_data.inst16;
    case Opcode::eType16_2: // passthrough
    case Opcode::eType32:
      return GetEndianSwap() ? llvm::byteswap<uint32_t>(m_data.inst32)
                             : m_data.inst32;
    case Opcode::eType64:
      return GetEndianSwap() ? llvm::byteswap<uint64_t>(m_data.inst64)
                             : m_data.inst64;
    case Opcode::eTypeBytes:
      break;
    }
    return invalid_opcode;
  }

  void SetOpcode8(uint8_t inst, lldb::ByteOrder order) {
    m_type = eType8;
    m_data.inst8 = inst;
    m_byte_order = order;
  }

  void SetOpcode16(uint16_t inst, lldb::ByteOrder order) {
    m_type = eType16;
    m_data.inst16 = inst;
    m_byte_order = order;
  }

  void SetOpcode16_2(uint32_t inst, lldb::ByteOrder order) {
    m_type = eType16_2;
    m_data.inst32 = inst;
    m_byte_order = order;
  }

  void SetOpcode32(uint32_t inst, lldb::ByteOrder order) {
    m_type = eType32;
    m_data.inst32 = inst;
    m_byte_order = order;
  }

  void SetOpcode64(uint64_t inst, lldb::ByteOrder order) {
    m_type = eType64;
    m_data.inst64 = inst;
    m_byte_order = order;
  }

  void SetOpcodeBytes(const void *bytes, size_t length) {
    if (bytes != nullptr && length > 0) {
      m_type = eTypeBytes;
      m_data.inst.length = length;
      assert(length < sizeof(m_data.inst.bytes));
      memcpy(m_data.inst.bytes, bytes, length);
      m_byte_order = lldb::eByteOrderInvalid;
    } else {
      m_type = eTypeInvalid;
      m_data.inst.length = 0;
    }
  }

  int Dump(Stream *s, uint32_t min_byte_width);

  const void *GetOpcodeBytes() const {
    return ((m_type == Opcode::eTypeBytes) ? m_data.inst.bytes : nullptr);
  }

  uint32_t GetByteSize() const {
    switch (m_type) {
    case Opcode::eTypeInvalid:
      break;
    case Opcode::eType8:
      return sizeof(m_data.inst8);
    case Opcode::eType16:
      return sizeof(m_data.inst16);
    case Opcode::eType16_2: // passthrough
    case Opcode::eType32:
      return sizeof(m_data.inst32);
    case Opcode::eType64:
      return sizeof(m_data.inst64);
    case Opcode::eTypeBytes:
      return m_data.inst.length;
    }
    return 0;
  }

  // Get the opcode exactly as it would be laid out in memory.
  uint32_t GetData(DataExtractor &data) const;

protected:
  friend class lldb::SBInstruction;

  const void *GetOpcodeDataBytes() const {
    switch (m_type) {
    case Opcode::eTypeInvalid:
      break;
    case Opcode::eType8:
      return &m_data.inst8;
    case Opcode::eType16:
      return &m_data.inst16;
    case Opcode::eType16_2: // passthrough
    case Opcode::eType32:
      return &m_data.inst32;
    case Opcode::eType64:
      return &m_data.inst64;
    case Opcode::eTypeBytes:
      return m_data.inst.bytes;
    }
    return nullptr;
  }

  lldb::ByteOrder GetDataByteOrder() const;

  bool GetEndianSwap() const {
    return (m_byte_order == lldb::eByteOrderBig &&
            endian::InlHostByteOrder() == lldb::eByteOrderLittle) ||
           (m_byte_order == lldb::eByteOrderLittle &&
            endian::InlHostByteOrder() == lldb::eByteOrderBig);
  }

  lldb::ByteOrder m_byte_order = lldb::eByteOrderInvalid;

  Opcode::Type m_type = eTypeInvalid;
  union {
    uint8_t inst8;
    uint16_t inst16;
    uint32_t inst32;
    uint64_t inst64;
    struct {
      uint8_t bytes[16]; // This must be big enough to handle any opcode for any
                         // supported target.
      uint8_t length;
    } inst;
  } m_data;
};

} // namespace lldb_private

#endif // LLDB_CORE_OPCODE_H
