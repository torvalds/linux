//===-- Stream.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Stream.h"

#include "lldb/Utility/AnsiTerminal.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/VASPrintf.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Regex.h"

#include <string>

#include <cinttypes>
#include <cstddef>

using namespace lldb;
using namespace lldb_private;

Stream::Stream(uint32_t flags, uint32_t addr_size, ByteOrder byte_order,
               bool colors)
    : m_flags(flags), m_addr_size(addr_size), m_byte_order(byte_order),
      m_forwarder(*this, colors) {}

Stream::Stream(bool colors)
    : m_flags(0), m_byte_order(endian::InlHostByteOrder()),
      m_forwarder(*this, colors) {}

// Destructor
Stream::~Stream() = default;

ByteOrder Stream::SetByteOrder(ByteOrder byte_order) {
  ByteOrder old_byte_order = m_byte_order;
  m_byte_order = byte_order;
  return old_byte_order;
}

// Put an offset "uval" out to the stream using the printf format in "format".
void Stream::Offset(uint32_t uval, const char *format) { Printf(format, uval); }

// Put an SLEB128 "uval" out to the stream using the printf format in "format".
size_t Stream::PutSLEB128(int64_t sval) {
  if (m_flags.Test(eBinary))
    return llvm::encodeSLEB128(sval, m_forwarder);
  else
    return Printf("0x%" PRIi64, sval);
}

// Put an ULEB128 "uval" out to the stream using the printf format in "format".
size_t Stream::PutULEB128(uint64_t uval) {
  if (m_flags.Test(eBinary))
    return llvm::encodeULEB128(uval, m_forwarder);
  else
    return Printf("0x%" PRIx64, uval);
}

// Print a raw NULL terminated C string to the stream.
size_t Stream::PutCString(llvm::StringRef str) {
  size_t bytes_written = 0;
  bytes_written = Write(str.data(), str.size());

  // when in binary mode, emit the NULL terminator
  if (m_flags.Test(eBinary))
    bytes_written += PutChar('\0');
  return bytes_written;
}

void Stream::PutCStringColorHighlighted(
    llvm::StringRef text, std::optional<HighlightSettings> pattern_info) {
  // Only apply color formatting when a pattern information is specified.
  // Otherwise, output the text without color formatting.
  if (!pattern_info.has_value()) {
    PutCString(text);
    return;
  }

  llvm::Regex reg_pattern(pattern_info->pattern);
  llvm::SmallVector<llvm::StringRef, 1> matches;
  llvm::StringRef remaining = text;
  std::string format_str = lldb_private::ansi::FormatAnsiTerminalCodes(
      pattern_info->prefix.str() + "%.*s" + pattern_info->suffix.str());
  while (reg_pattern.match(remaining, &matches)) {
    llvm::StringRef match = matches[0];
    size_t match_start_pos = match.data() - remaining.data();
    PutCString(remaining.take_front(match_start_pos));
    Printf(format_str.c_str(), match.size(), match.data());
    remaining = remaining.drop_front(match_start_pos + match.size());
  }
  if (remaining.size())
    PutCString(remaining);
}

// Print a double quoted NULL terminated C string to the stream using the
// printf format in "format".
void Stream::QuotedCString(const char *cstr, const char *format) {
  Printf(format, cstr);
}

// Put an address "addr" out to the stream with optional prefix and suffix
// strings.
void lldb_private::DumpAddress(llvm::raw_ostream &s, uint64_t addr,
                               uint32_t addr_size, const char *prefix,
                               const char *suffix) {
  if (prefix == nullptr)
    prefix = "";
  if (suffix == nullptr)
    suffix = "";
  s << prefix << llvm::format_hex(addr, 2 + 2 * addr_size) << suffix;
}

// Put an address range out to the stream with optional prefix and suffix
// strings.
void lldb_private::DumpAddressRange(llvm::raw_ostream &s, uint64_t lo_addr,
                                    uint64_t hi_addr, uint32_t addr_size,
                                    const char *prefix, const char *suffix) {
  if (prefix && prefix[0])
    s << prefix;
  DumpAddress(s, lo_addr, addr_size, "[");
  DumpAddress(s, hi_addr, addr_size, "-", ")");
  if (suffix && suffix[0])
    s << suffix;
}

size_t Stream::PutChar(char ch) { return Write(&ch, 1); }

// Print some formatted output to the stream.
size_t Stream::Printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  size_t result = PrintfVarArg(format, args);
  va_end(args);
  return result;
}

// Print some formatted output to the stream.
size_t Stream::PrintfVarArg(const char *format, va_list args) {
  llvm::SmallString<1024> buf;
  VASprintf(buf, format, args);

  // Include the NULL termination byte for binary output
  size_t length = buf.size();
  if (m_flags.Test(eBinary))
    ++length;
  return Write(buf.c_str(), length);
}

// Print and End of Line character to the stream
size_t Stream::EOL() { return PutChar('\n'); }

size_t Stream::Indent(llvm::StringRef str) {
  const size_t ind_length = PutCString(std::string(m_indent_level, ' '));
  const size_t str_length = PutCString(str);
  return ind_length + str_length;
}

// Stream a character "ch" out to this stream.
Stream &Stream::operator<<(char ch) {
  PutChar(ch);
  return *this;
}

// Stream the NULL terminated C string out to this stream.
Stream &Stream::operator<<(const char *s) {
  Printf("%s", s);
  return *this;
}

Stream &Stream::operator<<(llvm::StringRef str) {
  Write(str.data(), str.size());
  return *this;
}

// Stream the pointer value out to this stream.
Stream &Stream::operator<<(const void *p) {
  Printf("0x%.*tx", static_cast<int>(sizeof(const void *)) * 2, (ptrdiff_t)p);
  return *this;
}

// Get the current indentation level
unsigned Stream::GetIndentLevel() const { return m_indent_level; }

// Set the current indentation level
void Stream::SetIndentLevel(unsigned indent_level) {
  m_indent_level = indent_level;
}

// Increment the current indentation level
void Stream::IndentMore(unsigned amount) { m_indent_level += amount; }

// Decrement the current indentation level
void Stream::IndentLess(unsigned amount) {
  if (m_indent_level >= amount)
    m_indent_level -= amount;
  else
    m_indent_level = 0;
}

// Get the address size in bytes
uint32_t Stream::GetAddressByteSize() const { return m_addr_size; }

// Set the address size in bytes
void Stream::SetAddressByteSize(uint32_t addr_size) { m_addr_size = addr_size; }

// The flags get accessor
Flags &Stream::GetFlags() { return m_flags; }

// The flags const get accessor
const Flags &Stream::GetFlags() const { return m_flags; }

// The byte order get accessor

lldb::ByteOrder Stream::GetByteOrder() const { return m_byte_order; }

size_t Stream::PrintfAsRawHex8(const char *format, ...) {
  va_list args;
  va_start(args, format);

  llvm::SmallString<1024> buf;
  VASprintf(buf, format, args);

  ByteDelta delta(*this);
  for (char C : buf)
    _PutHex8(C, false);

  va_end(args);

  return *delta;
}

size_t Stream::PutNHex8(size_t n, uint8_t uvalue) {
  ByteDelta delta(*this);
  for (size_t i = 0; i < n; ++i)
    _PutHex8(uvalue, false);
  return *delta;
}

void Stream::_PutHex8(uint8_t uvalue, bool add_prefix) {
  if (m_flags.Test(eBinary)) {
    Write(&uvalue, 1);
  } else {
    if (add_prefix)
      PutCString("0x");

    static char g_hex_to_ascii_hex_char[16] = {'0', '1', '2', '3', '4', '5',
                                               '6', '7', '8', '9', 'a', 'b',
                                               'c', 'd', 'e', 'f'};
    char nibble_chars[2];
    nibble_chars[0] = g_hex_to_ascii_hex_char[(uvalue >> 4) & 0xf];
    nibble_chars[1] = g_hex_to_ascii_hex_char[(uvalue >> 0) & 0xf];
    Write(nibble_chars, sizeof(nibble_chars));
  }
}

size_t Stream::PutHex8(uint8_t uvalue) {
  ByteDelta delta(*this);
  _PutHex8(uvalue, false);
  return *delta;
}

size_t Stream::PutHex16(uint16_t uvalue, ByteOrder byte_order) {
  ByteDelta delta(*this);

  if (byte_order == eByteOrderInvalid)
    byte_order = m_byte_order;

  if (byte_order == eByteOrderLittle) {
    for (size_t byte = 0; byte < sizeof(uvalue); ++byte)
      _PutHex8(static_cast<uint8_t>(uvalue >> (byte * 8)), false);
  } else {
    for (size_t byte = sizeof(uvalue) - 1; byte < sizeof(uvalue); --byte)
      _PutHex8(static_cast<uint8_t>(uvalue >> (byte * 8)), false);
  }
  return *delta;
}

size_t Stream::PutHex32(uint32_t uvalue, ByteOrder byte_order) {
  ByteDelta delta(*this);

  if (byte_order == eByteOrderInvalid)
    byte_order = m_byte_order;

  if (byte_order == eByteOrderLittle) {
    for (size_t byte = 0; byte < sizeof(uvalue); ++byte)
      _PutHex8(static_cast<uint8_t>(uvalue >> (byte * 8)), false);
  } else {
    for (size_t byte = sizeof(uvalue) - 1; byte < sizeof(uvalue); --byte)
      _PutHex8(static_cast<uint8_t>(uvalue >> (byte * 8)), false);
  }
  return *delta;
}

size_t Stream::PutHex64(uint64_t uvalue, ByteOrder byte_order) {
  ByteDelta delta(*this);

  if (byte_order == eByteOrderInvalid)
    byte_order = m_byte_order;

  if (byte_order == eByteOrderLittle) {
    for (size_t byte = 0; byte < sizeof(uvalue); ++byte)
      _PutHex8(static_cast<uint8_t>(uvalue >> (byte * 8)), false);
  } else {
    for (size_t byte = sizeof(uvalue) - 1; byte < sizeof(uvalue); --byte)
      _PutHex8(static_cast<uint8_t>(uvalue >> (byte * 8)), false);
  }
  return *delta;
}

size_t Stream::PutMaxHex64(uint64_t uvalue, size_t byte_size,
                           lldb::ByteOrder byte_order) {
  switch (byte_size) {
  case 1:
    return PutHex8(static_cast<uint8_t>(uvalue));
  case 2:
    return PutHex16(static_cast<uint16_t>(uvalue), byte_order);
  case 4:
    return PutHex32(static_cast<uint32_t>(uvalue), byte_order);
  case 8:
    return PutHex64(uvalue, byte_order);
  }
  return 0;
}

size_t Stream::PutPointer(void *ptr) {
  return PutRawBytes(&ptr, sizeof(ptr), endian::InlHostByteOrder(),
                     endian::InlHostByteOrder());
}

size_t Stream::PutFloat(float f, ByteOrder byte_order) {
  if (byte_order == eByteOrderInvalid)
    byte_order = m_byte_order;

  return PutRawBytes(&f, sizeof(f), endian::InlHostByteOrder(), byte_order);
}

size_t Stream::PutDouble(double d, ByteOrder byte_order) {
  if (byte_order == eByteOrderInvalid)
    byte_order = m_byte_order;

  return PutRawBytes(&d, sizeof(d), endian::InlHostByteOrder(), byte_order);
}

size_t Stream::PutLongDouble(long double ld, ByteOrder byte_order) {
  if (byte_order == eByteOrderInvalid)
    byte_order = m_byte_order;

  return PutRawBytes(&ld, sizeof(ld), endian::InlHostByteOrder(), byte_order);
}

size_t Stream::PutRawBytes(const void *s, size_t src_len,
                           ByteOrder src_byte_order, ByteOrder dst_byte_order) {
  ByteDelta delta(*this);

  if (src_byte_order == eByteOrderInvalid)
    src_byte_order = m_byte_order;

  if (dst_byte_order == eByteOrderInvalid)
    dst_byte_order = m_byte_order;

  const uint8_t *src = static_cast<const uint8_t *>(s);
  bool binary_was_set = m_flags.Test(eBinary);
  if (!binary_was_set)
    m_flags.Set(eBinary);
  if (src_byte_order == dst_byte_order) {
    for (size_t i = 0; i < src_len; ++i)
      _PutHex8(src[i], false);
  } else {
    for (size_t i = src_len; i > 0; --i)
      _PutHex8(src[i - 1], false);
  }
  if (!binary_was_set)
    m_flags.Clear(eBinary);

  return *delta;
}

size_t Stream::PutBytesAsRawHex8(const void *s, size_t src_len,
                                 ByteOrder src_byte_order,
                                 ByteOrder dst_byte_order) {
  ByteDelta delta(*this);

  if (src_byte_order == eByteOrderInvalid)
    src_byte_order = m_byte_order;

  if (dst_byte_order == eByteOrderInvalid)
    dst_byte_order = m_byte_order;

  const uint8_t *src = static_cast<const uint8_t *>(s);
  bool binary_is_set = m_flags.Test(eBinary);
  m_flags.Clear(eBinary);
  if (src_byte_order == dst_byte_order) {
    for (size_t i = 0; i < src_len; ++i)
      _PutHex8(src[i], false);
  } else {
    for (size_t i = src_len; i > 0; --i)
      _PutHex8(src[i - 1], false);
  }
  if (binary_is_set)
    m_flags.Set(eBinary);

  return *delta;
}

size_t Stream::PutStringAsRawHex8(llvm::StringRef s) {
  ByteDelta delta(*this);
  bool binary_is_set = m_flags.Test(eBinary);
  m_flags.Clear(eBinary);
  for (char c : s)
    _PutHex8(c, false);
  if (binary_is_set)
    m_flags.Set(eBinary);
  return *delta;
}
