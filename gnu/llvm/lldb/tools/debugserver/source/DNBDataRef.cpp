//===-- DNBDataRef.cpp ------------------------------------------*- C++ -*-===//
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

#include "DNBDataRef.h"
#include "DNBLog.h"
#include <cassert>
#include <cctype>
#include <libkern/OSByteOrder.h>

// Constructor

DNBDataRef::DNBDataRef()
    : m_start(NULL), m_end(NULL), m_swap(false), m_ptrSize(0),
      m_addrPCRelative(INVALID_NUB_ADDRESS), m_addrTEXT(INVALID_NUB_ADDRESS),
      m_addrDATA(INVALID_NUB_ADDRESS) {}

// Constructor

DNBDataRef::DNBDataRef(const uint8_t *start, size_t size, bool swap)
    : m_start(start), m_end(start + size), m_swap(swap), m_ptrSize(0),
      m_addrPCRelative(INVALID_NUB_ADDRESS), m_addrTEXT(INVALID_NUB_ADDRESS),
      m_addrDATA(INVALID_NUB_ADDRESS) {}

// Destructor

DNBDataRef::~DNBDataRef() = default;

// Get8
uint8_t DNBDataRef::Get8(offset_t *offset_ptr) const {
  uint8_t val = 0;
  if (ValidOffsetForDataOfSize(*offset_ptr, sizeof(val))) {
    val = *(m_start + *offset_ptr);
    *offset_ptr += sizeof(val);
  }
  return val;
}

// Get16
uint16_t DNBDataRef::Get16(offset_t *offset_ptr) const {
  uint16_t val = 0;
  if (ValidOffsetForDataOfSize(*offset_ptr, sizeof(val))) {
    const uint8_t *p = m_start + *offset_ptr;
    memcpy(&val, p, sizeof(uint16_t));

    if (m_swap)
      val = OSSwapInt16(val);

    // Advance the offset
    *offset_ptr += sizeof(val);
  }
  return val;
}

// Get32
uint32_t DNBDataRef::Get32(offset_t *offset_ptr) const {
  uint32_t val = 0;
  if (ValidOffsetForDataOfSize(*offset_ptr, sizeof(val))) {
    const uint8_t *p = m_start + *offset_ptr;
    memcpy(&val, p, sizeof(uint32_t));
    if (m_swap)
      val = OSSwapInt32(val);

    // Advance the offset
    *offset_ptr += sizeof(val);
  }
  return val;
}

// Get64
uint64_t DNBDataRef::Get64(offset_t *offset_ptr) const {
  uint64_t val = 0;
  if (ValidOffsetForDataOfSize(*offset_ptr, sizeof(val))) {
    const uint8_t *p = m_start + *offset_ptr;
    memcpy(&val, p, sizeof(uint64_t));
    if (m_swap)
      val = OSSwapInt64(val);

    // Advance the offset
    *offset_ptr += sizeof(val);
  }
  return val;
}

// GetMax32
//
// Used for calls when the size can vary. Fill in extra cases if they
// are ever needed.
uint32_t DNBDataRef::GetMax32(offset_t *offset_ptr, uint32_t byte_size) const {
  switch (byte_size) {
  case 1:
    return Get8(offset_ptr);
    break;
  case 2:
    return Get16(offset_ptr);
    break;
  case 4:
    return Get32(offset_ptr);
    break;
  default:
    assert(false && "GetMax32 unhandled case!");
    break;
  }
  return 0;
}

// GetMax64
//
// Used for calls when the size can vary. Fill in extra cases if they
// are ever needed.
uint64_t DNBDataRef::GetMax64(offset_t *offset_ptr, uint32_t size) const {
  switch (size) {
  case 1:
    return Get8(offset_ptr);
    break;
  case 2:
    return Get16(offset_ptr);
    break;
  case 4:
    return Get32(offset_ptr);
    break;
  case 8:
    return Get64(offset_ptr);
    break;
  default:
    assert(false && "GetMax64 unhandled case!");
    break;
  }
  return 0;
}

// GetPointer
//
// Extract a pointer value from the buffer. The pointer size must be
// set prior to using this using one of the SetPointerSize functions.
uint64_t DNBDataRef::GetPointer(offset_t *offset_ptr) const {
  // Must set pointer size prior to using this call
  assert(m_ptrSize != 0);
  return GetMax64(offset_ptr, m_ptrSize);
}
// GetCStr
const char *DNBDataRef::GetCStr(offset_t *offset_ptr,
                                uint32_t fixed_length) const {
  const char *s = NULL;
  if (m_start < m_end) {
    s = (const char *)m_start + *offset_ptr;

    // Advance the offset
    if (fixed_length)
      *offset_ptr += fixed_length;
    else
      *offset_ptr += strlen(s) + 1;
  }
  return s;
}

// GetData
const uint8_t *DNBDataRef::GetData(offset_t *offset_ptr,
                                   uint32_t length) const {
  const uint8_t *data = NULL;
  if (length > 0 && ValidOffsetForDataOfSize(*offset_ptr, length)) {
    data = m_start + *offset_ptr;
    *offset_ptr += length;
  }
  return data;
}

// Get_ULEB128
uint64_t DNBDataRef::Get_ULEB128(offset_t *offset_ptr) const {
  uint64_t result = 0;
  if (m_start < m_end) {
    int shift = 0;
    const uint8_t *src = m_start + *offset_ptr;
    uint8_t byte;
    int bytecount = 0;

    while (src < m_end) {
      bytecount++;
      byte = *src++;
      result |= (uint64_t)(byte & 0x7f) << shift;
      shift += 7;
      if ((byte & 0x80) == 0)
        break;
    }

    *offset_ptr += bytecount;
  }
  return result;
}

// Get_SLEB128
int64_t DNBDataRef::Get_SLEB128(offset_t *offset_ptr) const {
  int64_t result = 0;

  if (m_start < m_end) {
    int shift = 0;
    int size = sizeof(uint32_t) * 8;
    const uint8_t *src = m_start + *offset_ptr;

    uint8_t byte = 0;
    int bytecount = 0;

    while (src < m_end) {
      bytecount++;
      byte = *src++;
      result |= (int64_t)(byte & 0x7f) << shift;
      shift += 7;
      if ((byte & 0x80) == 0)
        break;
    }

    // Sign bit of byte is 2nd high order bit (0x40)
    if (shift < size && (byte & 0x40))
      result |= -(1ll << shift);

    *offset_ptr += bytecount;
  }
  return result;
}

// Skip_LEB128
//
// Skips past ULEB128 and SLEB128 numbers (just updates the offset)
void DNBDataRef::Skip_LEB128(offset_t *offset_ptr) const {
  if (m_start < m_end) {
    const uint8_t *start = m_start + *offset_ptr;
    const uint8_t *src = start;

    while ((src < m_end) && (*src++ & 0x80))
      /* Do nothing */;

    *offset_ptr += src - start;
  }
}

uint32_t DNBDataRef::Dump(uint32_t startOffset, uint32_t endOffset,
                          uint64_t offsetBase, DNBDataRef::Type type,
                          uint32_t numPerLine, const char *format) {
  uint32_t offset;
  uint32_t count;
  char str[1024];
  str[0] = '\0';
  size_t str_offset = 0;

  for (offset = startOffset, count = 0;
       ValidOffset(offset) && offset < endOffset; ++count) {
    if ((count % numPerLine) == 0) {
      // Print out any previous string
      if (str[0] != '\0')
        DNBLog("%s", str);
      // Reset string offset and fill the current line string with address:
      str_offset = 0;
      str_offset += snprintf(str, sizeof(str), "0x%8.8llx:",
                             (uint64_t)(offsetBase + (offset - startOffset)));
    }

    // Make sure we don't pass the bounds of our current string buffer on each
    // iteration through this loop
    if (str_offset >= sizeof(str)) {
      // The last snprintf consumed our string buffer, we will need to dump this
      // out
      // and reset the string with no address
      DNBLog("%s", str);
      str_offset = 0;
      str[0] = '\0';
    }

    // We already checked that there is at least some room in the string str
    // above, so it is safe to make
    // the snprintf call each time through this loop
    switch (type) {
    case TypeUInt8:
      str_offset += snprintf(str + str_offset, sizeof(str) - str_offset,
                             format ? format : " %2.2x", Get8(&offset));
      break;
    case TypeChar: {
      char ch = Get8(&offset);
      str_offset += snprintf(str + str_offset, sizeof(str) - str_offset,
                             format ? format : " %c", isprint(ch) ? ch : ' ');
    } break;
    case TypeUInt16:
      str_offset += snprintf(str + str_offset, sizeof(str) - str_offset,
                             format ? format : " %4.4x", Get16(&offset));
      break;
    case TypeUInt32:
      str_offset += snprintf(str + str_offset, sizeof(str) - str_offset,
                             format ? format : " %8.8x", Get32(&offset));
      break;
    case TypeUInt64:
      str_offset += snprintf(str + str_offset, sizeof(str) - str_offset,
                             format ? format : " %16.16llx", Get64(&offset));
      break;
    case TypePointer:
      str_offset += snprintf(str + str_offset, sizeof(str) - str_offset,
                             format ? format : " 0x%llx", GetPointer(&offset));
      break;
    case TypeULEB128:
      str_offset += snprintf(str + str_offset, sizeof(str) - str_offset,
                             format ? format : " 0x%llx", Get_ULEB128(&offset));
      break;
    case TypeSLEB128:
      str_offset += snprintf(str + str_offset, sizeof(str) - str_offset,
                             format ? format : " %lld", Get_SLEB128(&offset));
      break;
    }
  }

  if (str[0] != '\0')
    DNBLog("%s", str);

  return offset; // Return the offset at which we ended up
}
