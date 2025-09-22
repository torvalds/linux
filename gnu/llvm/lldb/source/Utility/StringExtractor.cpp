//===-- StringExtractor.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/StringExtractor.h"
#include "llvm/ADT/StringExtras.h"

#include <tuple>

#include <cctype>
#include <cstdlib>
#include <cstring>

static inline int xdigit_to_sint(char ch) {
  if (ch >= 'a' && ch <= 'f')
    return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F')
    return 10 + ch - 'A';
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  return -1;
}

// StringExtractor constructor
StringExtractor::StringExtractor() : m_packet() {}

StringExtractor::StringExtractor(llvm::StringRef packet_str) : m_packet() {
  m_packet.assign(packet_str.begin(), packet_str.end());
}

StringExtractor::StringExtractor(const char *packet_cstr) : m_packet() {
  if (packet_cstr)
    m_packet.assign(packet_cstr);
}

// Destructor
StringExtractor::~StringExtractor() = default;

char StringExtractor::GetChar(char fail_value) {
  if (m_index < m_packet.size()) {
    char ch = m_packet[m_index];
    ++m_index;
    return ch;
  }
  m_index = UINT64_MAX;
  return fail_value;
}

// If a pair of valid hex digits exist at the head of the StringExtractor they
// are decoded into an unsigned byte and returned by this function
//
// If there is not a pair of valid hex digits at the head of the
// StringExtractor, it is left unchanged and -1 is returned
int StringExtractor::DecodeHexU8() {
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
  return static_cast<uint8_t>((hi_nibble << 4) + lo_nibble);
}

// Extract an unsigned character from two hex ASCII chars in the packet string,
// or return fail_value on failure
uint8_t StringExtractor::GetHexU8(uint8_t fail_value, bool set_eof_on_fail) {
  // On success, fail_value will be overwritten with the next character in the
  // stream
  GetHexU8Ex(fail_value, set_eof_on_fail);
  return fail_value;
}

bool StringExtractor::GetHexU8Ex(uint8_t &ch, bool set_eof_on_fail) {
  int byte = DecodeHexU8();
  if (byte == -1) {
    if (set_eof_on_fail || m_index >= m_packet.size())
      m_index = UINT64_MAX;
    // ch should not be changed in case of failure
    return false;
  }
  ch = static_cast<uint8_t>(byte);
  return true;
}

uint32_t StringExtractor::GetU32(uint32_t fail_value, int base) {
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

int32_t StringExtractor::GetS32(int32_t fail_value, int base) {
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

uint64_t StringExtractor::GetU64(uint64_t fail_value, int base) {
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

int64_t StringExtractor::GetS64(int64_t fail_value, int base) {
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

uint32_t StringExtractor::GetHexMaxU32(bool little_endian,
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
        result |= (static_cast<uint32_t>(nibble_hi) << (shift_amount + 4));
        result |= (static_cast<uint32_t>(nibble_lo) << shift_amount);
        nibble_count += 2;
        shift_amount += 8;
      } else {
        result |= (static_cast<uint32_t>(nibble_hi) << shift_amount);
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

uint64_t StringExtractor::GetHexMaxU64(bool little_endian,
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
        result |= (static_cast<uint64_t>(nibble_hi) << (shift_amount + 4));
        result |= (static_cast<uint64_t>(nibble_lo) << shift_amount);
        nibble_count += 2;
        shift_amount += 8;
      } else {
        result |= (static_cast<uint64_t>(nibble_hi) << shift_amount);
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

bool StringExtractor::ConsumeFront(const llvm::StringRef &str) {
  llvm::StringRef S = GetStringRef();
  if (!S.starts_with(str))
    return false;
  else
    m_index += str.size();
  return true;
}

size_t StringExtractor::GetHexBytes(llvm::MutableArrayRef<uint8_t> dest,
                                    uint8_t fail_fill_value) {
  size_t bytes_extracted = 0;
  while (!dest.empty() && GetBytesLeft() > 0) {
    dest[0] = GetHexU8(fail_fill_value);
    if (!IsGood())
      break;
    ++bytes_extracted;
    dest = dest.drop_front();
  }

  if (!dest.empty())
    ::memset(dest.data(), fail_fill_value, dest.size());

  return bytes_extracted;
}

// Decodes all valid hex encoded bytes at the head of the StringExtractor,
// limited by dst_len.
//
// Returns the number of bytes successfully decoded
size_t StringExtractor::GetHexBytesAvail(llvm::MutableArrayRef<uint8_t> dest) {
  size_t bytes_extracted = 0;
  while (!dest.empty()) {
    int decode = DecodeHexU8();
    if (decode == -1)
      break;
    dest[0] = static_cast<uint8_t>(decode);
    dest = dest.drop_front();
    ++bytes_extracted;
  }
  return bytes_extracted;
}

size_t StringExtractor::GetHexByteString(std::string &str) {
  str.clear();
  str.reserve(GetBytesLeft() / 2);
  char ch;
  while ((ch = GetHexU8()) != '\0')
    str.append(1, ch);
  return str.size();
}

size_t StringExtractor::GetHexByteStringFixedLength(std::string &str,
                                                    uint32_t nibble_length) {
  str.clear();

  uint32_t nibble_count = 0;
  for (const char *pch = Peek();
       (nibble_count < nibble_length) && (pch != nullptr);
       str.append(1, GetHexU8(0, false)), pch = Peek(), nibble_count += 2) {
  }

  return str.size();
}

size_t StringExtractor::GetHexByteStringTerminatedBy(std::string &str,
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

bool StringExtractor::GetNameColonValue(llvm::StringRef &name,
                                        llvm::StringRef &value) {
  // Read something in the form of NNNN:VVVV; where NNNN is any character that
  // is not a colon, followed by a ':' character, then a value (one or more ';'
  // chars), followed by a ';'
  if (m_index >= m_packet.size())
    return fail();

  llvm::StringRef view(m_packet);
  if (view.empty())
    return fail();

  llvm::StringRef a, b, c, d;
  view = view.substr(m_index);
  std::tie(a, b) = view.split(':');
  if (a.empty() || b.empty())
    return fail();
  std::tie(c, d) = b.split(';');
  if (b == c && d.empty())
    return fail();

  name = a;
  value = c;
  if (d.empty())
    m_index = m_packet.size();
  else {
    size_t bytes_consumed = d.data() - view.data();
    m_index += bytes_consumed;
  }
  return true;
}

void StringExtractor::SkipSpaces() {
  const size_t n = m_packet.size();
  while (m_index < n && llvm::isSpace(m_packet[m_index]))
    ++m_index;
}
