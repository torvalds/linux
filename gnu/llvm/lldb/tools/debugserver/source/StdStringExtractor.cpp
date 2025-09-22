//===-- StdStringExtractor.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "StdStringExtractor.h"

#include <cstdlib>

static inline int xdigit_to_sint(char ch) {
  if (ch >= 'a' && ch <= 'f')
    return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F')
    return 10 + ch - 'A';
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  return -1;
}

// StdStringExtractor constructor
StdStringExtractor::StdStringExtractor() : m_packet(), m_index(0) {}

StdStringExtractor::StdStringExtractor(const char *packet_cstr)
    : m_packet(), m_index(0) {
  if (packet_cstr)
    m_packet.assign(packet_cstr);
}

// Destructor
StdStringExtractor::~StdStringExtractor() = default;

char StdStringExtractor::GetChar(char fail_value) {
  if (m_index < m_packet.size()) {
    char ch = m_packet[m_index];
    ++m_index;
    return ch;
  }
  m_index = UINT64_MAX;
  return fail_value;
}

// If a pair of valid hex digits exist at the head of the
// StdStringExtractor they are decoded into an unsigned byte and returned
// by this function
//
// If there is not a pair of valid hex digits at the head of the
// StdStringExtractor, it is left unchanged and -1 is returned
int StdStringExtractor::DecodeHexU8() {
  SkipSpaces();
  if (GetBytesLeft() < 2) {
    return -1;
  }
  const int hi_nibble = xdigit_to_sint(m_packet[m_index]);
  const int lo_nibble = xdigit_to_sint(m_packet[m_index + 1]);
  if (hi_nibble == -1 || lo_nibble == -1) {
    return -1;
  }
  m_index += 2;
  return (uint8_t)((hi_nibble << 4) + lo_nibble);
}

// Extract an unsigned character from two hex ASCII chars in the packet
// string, or return fail_value on failure
uint8_t StdStringExtractor::GetHexU8(uint8_t fail_value, bool set_eof_on_fail) {
  // On success, fail_value will be overwritten with the next
  // character in the stream
  GetHexU8Ex(fail_value, set_eof_on_fail);
  return fail_value;
}

bool StdStringExtractor::GetHexU8Ex(uint8_t &ch, bool set_eof_on_fail) {
  int byte = DecodeHexU8();
  if (byte == -1) {
    if (set_eof_on_fail || m_index >= m_packet.size())
      m_index = UINT64_MAX;
    // ch should not be changed in case of failure
    return false;
  }
  ch = (uint8_t)byte;
  return true;
}

uint32_t StdStringExtractor::GetU32(uint32_t fail_value, int base) {
  if (m_index < m_packet.size()) {
    char *end = nullptr;
    const char *start = m_packet.c_str();
    const char *cstr = start + m_index;
    uint32_t result = static_cast<uint32_t>(::strtoul(cstr, &end, base));

    if (end && end != cstr) {
      m_index = end - start;
      return result;
    }
  }
  return fail_value;
}

int32_t StdStringExtractor::GetS32(int32_t fail_value, int base) {
  if (m_index < m_packet.size()) {
    char *end = nullptr;
    const char *start = m_packet.c_str();
    const char *cstr = start + m_index;
    int32_t result = static_cast<int32_t>(::strtol(cstr, &end, base));

    if (end && end != cstr) {
      m_index = end - start;
      return result;
    }
  }
  return fail_value;
}

uint64_t StdStringExtractor::GetU64(uint64_t fail_value, int base) {
  if (m_index < m_packet.size()) {
    char *end = nullptr;
    const char *start = m_packet.c_str();
    const char *cstr = start + m_index;
    uint64_t result = ::strtoull(cstr, &end, base);

    if (end && end != cstr) {
      m_index = end - start;
      return result;
    }
  }
  return fail_value;
}

int64_t StdStringExtractor::GetS64(int64_t fail_value, int base) {
  if (m_index < m_packet.size()) {
    char *end = nullptr;
    const char *start = m_packet.c_str();
    const char *cstr = start + m_index;
    int64_t result = ::strtoll(cstr, &end, base);

    if (end && end != cstr) {
      m_index = end - start;
      return result;
    }
  }
  return fail_value;
}

uint32_t StdStringExtractor::GetHexMaxU32(bool little_endian,
                                          uint32_t fail_value) {
  uint32_t result = 0;
  uint32_t nibble_count = 0;

  SkipSpaces();
  if (little_endian) {
    uint32_t shift_amount = 0;
    while (m_index < m_packet.size() && ::isxdigit(m_packet[m_index])) {
      // Make sure we don't exceed the size of a uint32_t...
      if (nibble_count >= (sizeof(uint32_t) * 2)) {
        m_index = UINT64_MAX;
        return fail_value;
      }

      uint8_t nibble_lo;
      uint8_t nibble_hi = xdigit_to_sint(m_packet[m_index]);
      ++m_index;
      if (m_index < m_packet.size() && ::isxdigit(m_packet[m_index])) {
        nibble_lo = xdigit_to_sint(m_packet[m_index]);
        ++m_index;
        result |= ((uint32_t)nibble_hi << (shift_amount + 4));
        result |= ((uint32_t)nibble_lo << shift_amount);
        nibble_count += 2;
        shift_amount += 8;
      } else {
        result |= ((uint32_t)nibble_hi << shift_amount);
        nibble_count += 1;
        shift_amount += 4;
      }
    }
  } else {
    while (m_index < m_packet.size() && ::isxdigit(m_packet[m_index])) {
      // Make sure we don't exceed the size of a uint32_t...
      if (nibble_count >= (sizeof(uint32_t) * 2)) {
        m_index = UINT64_MAX;
        return fail_value;
      }

      uint8_t nibble = xdigit_to_sint(m_packet[m_index]);
      // Big Endian
      result <<= 4;
      result |= nibble;

      ++m_index;
      ++nibble_count;
    }
  }
  return result;
}

uint64_t StdStringExtractor::GetHexMaxU64(bool little_endian,
                                          uint64_t fail_value) {
  uint64_t result = 0;
  uint32_t nibble_count = 0;

  SkipSpaces();
  if (little_endian) {
    uint32_t shift_amount = 0;
    while (m_index < m_packet.size() && ::isxdigit(m_packet[m_index])) {
      // Make sure we don't exceed the size of a uint64_t...
      if (nibble_count >= (sizeof(uint64_t) * 2)) {
        m_index = UINT64_MAX;
        return fail_value;
      }

      uint8_t nibble_lo;
      uint8_t nibble_hi = xdigit_to_sint(m_packet[m_index]);
      ++m_index;
      if (m_index < m_packet.size() && ::isxdigit(m_packet[m_index])) {
        nibble_lo = xdigit_to_sint(m_packet[m_index]);
        ++m_index;
        result |= ((uint64_t)nibble_hi << (shift_amount + 4));
        result |= ((uint64_t)nibble_lo << shift_amount);
        nibble_count += 2;
        shift_amount += 8;
      } else {
        result |= ((uint64_t)nibble_hi << shift_amount);
        nibble_count += 1;
        shift_amount += 4;
      }
    }
  } else {
    while (m_index < m_packet.size() && ::isxdigit(m_packet[m_index])) {
      // Make sure we don't exceed the size of a uint64_t...
      if (nibble_count >= (sizeof(uint64_t) * 2)) {
        m_index = UINT64_MAX;
        return fail_value;
      }

      uint8_t nibble = xdigit_to_sint(m_packet[m_index]);
      // Big Endian
      result <<= 4;
      result |= nibble;

      ++m_index;
      ++nibble_count;
    }
  }
  return result;
}

size_t StdStringExtractor::GetHexBytes(void *dst_void, size_t dst_len,
                                       uint8_t fail_fill_value) {
  uint8_t *dst = (uint8_t *)dst_void;
  size_t bytes_extracted = 0;
  while (bytes_extracted < dst_len && GetBytesLeft()) {
    dst[bytes_extracted] = GetHexU8(fail_fill_value);
    if (IsGood())
      ++bytes_extracted;
    else
      break;
  }

  for (size_t i = bytes_extracted; i < dst_len; ++i)
    dst[i] = fail_fill_value;

  return bytes_extracted;
}

// Decodes all valid hex encoded bytes at the head of the
// StdStringExtractor, limited by dst_len.
//
// Returns the number of bytes successfully decoded
size_t StdStringExtractor::GetHexBytesAvail(void *dst_void, size_t dst_len) {
  uint8_t *dst = (uint8_t *)dst_void;
  size_t bytes_extracted = 0;
  while (bytes_extracted < dst_len) {
    int decode = DecodeHexU8();
    if (decode == -1) {
      break;
    }
    dst[bytes_extracted++] = (uint8_t)decode;
  }
  return bytes_extracted;
}

size_t StdStringExtractor::GetHexByteString(std::string &str) {
  str.clear();
  str.reserve(GetBytesLeft() / 2);
  char ch;
  while ((ch = GetHexU8()) != '\0')
    str.append(1, ch);
  return str.size();
}

size_t StdStringExtractor::GetHexByteStringFixedLength(std::string &str,
                                                       uint32_t nibble_length) {
  str.clear();

  uint32_t nibble_count = 0;
  for (const char *pch = Peek();
       (nibble_count < nibble_length) && (pch != nullptr);
       str.append(1, GetHexU8(0, false)), pch = Peek(), nibble_count += 2) {
  }

  return str.size();
}

size_t StdStringExtractor::GetHexByteStringTerminatedBy(std::string &str,
                                                        char terminator) {
  str.clear();
  char ch;
  while ((ch = GetHexU8(0, false)) != '\0')
    str.append(1, ch);
  if (Peek() && *Peek() == terminator)
    return str.size();

  str.clear();
  return str.size();
}

bool StdStringExtractor::GetNameColonValue(std::string &name,
                                           std::string &value) {
  // Read something in the form of NNNN:VVVV; where NNNN is any character
  // that is not a colon, followed by a ':' character, then a value (one or
  // more ';' chars), followed by a ';'
  if (m_index < m_packet.size()) {
    const size_t colon_idx = m_packet.find(':', m_index);
    if (colon_idx != std::string::npos) {
      const size_t semicolon_idx = m_packet.find(';', colon_idx);
      if (semicolon_idx != std::string::npos) {
        name.assign(m_packet, m_index, colon_idx - m_index);
        value.assign(m_packet, colon_idx + 1, semicolon_idx - (colon_idx + 1));
        m_index = semicolon_idx + 1;
        return true;
      }
    }
  }
  m_index = UINT64_MAX;
  return false;
}

void StdStringExtractor::SkipSpaces() {
  const size_t n = m_packet.size();
  while (m_index < n && isspace(m_packet[m_index]))
    ++m_index;
}
