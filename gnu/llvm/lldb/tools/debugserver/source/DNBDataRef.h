//===-- DNBDataRef.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 1/11/06.
//
//===----------------------------------------------------------------------===//
//
//  DNBDataRef is a class that can extract data in normal or byte
//  swapped order from a data buffer that someone else owns. The data
//  buffer needs to remain intact as long as the DNBDataRef object
//  needs the data. Strings returned are pointers into the data buffer
//  and will need to be copied if they are needed after the data buffer
//  is no longer around.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBDATAREF_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBDATAREF_H

#include "DNBDefs.h"
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

class DNBDataRef {
public:
  // For use with Dump
  enum Type {
    TypeUInt8 = 0,
    TypeChar,
    TypeUInt16,
    TypeUInt32,
    TypeUInt64,
    TypePointer,
    TypeULEB128,
    TypeSLEB128
  };
  typedef uint32_t offset_t;
  typedef nub_addr_t addr_t;

  DNBDataRef();
  DNBDataRef(const uint8_t *start, size_t size, bool swap);
  ~DNBDataRef();
  void Clear() {
    DNBDataRef::SetData(NULL, 0);
    m_swap = false;
  }

  size_t BytesLeft(size_t offset) const {
    const size_t size = GetSize();
    if (size > offset)
      return size - offset;
    return 0;
  }

  bool ValidOffset(offset_t offset) const { return BytesLeft(offset) > 0; }
  bool ValidOffsetForDataOfSize(offset_t offset, uint32_t num_bytes) const {
    return num_bytes <= BytesLeft(offset);
  }
  size_t GetSize() const { return m_end - m_start; }
  const uint8_t *GetDataStart() const { return m_start; }
  const uint8_t *GetDataEnd() const { return m_end; }
  bool GetSwap() const { return m_swap; }
  void SetSwap(bool swap) { m_swap = swap; }
  void SetData(const uint8_t *start, size_t size) {
    m_start = start;
    if (m_start != NULL)
      m_end = start + size;
    else
      m_end = NULL;
  }
  uint8_t GetPointerSize() const { return m_ptrSize; }
  void SetPointerSize(uint8_t size) { m_ptrSize = size; }
  void SetEHPtrBaseAddrPCRelative(addr_t addr = INVALID_NUB_ADDRESS) {
    m_addrPCRelative = addr;
  }
  void SetEHPtrBaseAddrTEXT(addr_t addr = INVALID_NUB_ADDRESS) {
    m_addrTEXT = addr;
  }
  void SetEHPtrBaseAddrDATA(addr_t addr = INVALID_NUB_ADDRESS) {
    m_addrDATA = addr;
  }
  uint8_t Get8(offset_t *offset_ptr) const;
  uint16_t Get16(offset_t *offset_ptr) const;
  uint32_t Get32(offset_t *offset_ptr) const;
  uint64_t Get64(offset_t *offset_ptr) const;
  uint32_t GetMax32(offset_t *offset_ptr, uint32_t byte_size) const;
  uint64_t GetMax64(offset_t *offset_ptr, uint32_t byte_size) const;
  uint64_t GetPointer(offset_t *offset_ptr) const;
  //  uint64_t        GetDwarfEHPtr(offset_t *offset_ptr, uint32_t eh_ptr_enc)
  //  const;
  const char *GetCStr(offset_t *offset_ptr, uint32_t fixed_length = 0) const;
  const char *PeekCStr(offset_t offset) const {
    if (ValidOffset(offset))
      return (const char *)m_start + offset;
    return NULL;
  }

  const uint8_t *GetData(offset_t *offset_ptr, uint32_t length) const;
  uint64_t Get_ULEB128(offset_t *offset_ptr) const;
  int64_t Get_SLEB128(offset_t *offset_ptr) const;
  void Skip_LEB128(offset_t *offset_ptr) const;

  uint32_t Dump(offset_t startOffset, offset_t endOffset, uint64_t offsetBase,
                DNBDataRef::Type type, uint32_t numPerLine,
                const char *typeFormat = NULL);

protected:
  const uint8_t *m_start;
  const uint8_t *m_end;
  bool m_swap;
  uint8_t m_ptrSize;
  addr_t m_addrPCRelative;
  addr_t m_addrTEXT;
  addr_t m_addrDATA;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBDATAREF_H
