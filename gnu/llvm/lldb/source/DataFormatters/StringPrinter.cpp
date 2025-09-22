//===-- StringPrinter.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/StringPrinter.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Status.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"

#include <cctype>
#include <locale>
#include <memory>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;
using GetPrintableElementType = StringPrinter::GetPrintableElementType;
using StringElementType = StringPrinter::StringElementType;

/// DecodedCharBuffer stores the decoded contents of a single character. It
/// avoids managing memory on the heap by copying decoded bytes into an in-line
/// buffer.
class DecodedCharBuffer {
public:
  DecodedCharBuffer(std::nullptr_t) {}

  DecodedCharBuffer(const uint8_t *bytes, size_t size) : m_size(size) {
    if (size > MaxLength)
      llvm_unreachable("unsupported length");
    memcpy(m_data, bytes, size);
  }

  DecodedCharBuffer(const char *bytes, size_t size)
      : DecodedCharBuffer(reinterpret_cast<const uint8_t *>(bytes), size) {}

  const uint8_t *GetBytes() const { return m_data; }

  size_t GetSize() const { return m_size; }

private:
  static constexpr unsigned MaxLength = 16;

  size_t m_size = 0;
  uint8_t m_data[MaxLength] = {0};
};

using EscapingHelper =
    std::function<DecodedCharBuffer(uint8_t *, uint8_t *, uint8_t *&)>;

// we define this for all values of type but only implement it for those we
// care about that's good because we get linker errors for any unsupported type
template <StringElementType type>
static DecodedCharBuffer
GetPrintableImpl(uint8_t *buffer, uint8_t *buffer_end, uint8_t *&next,
                 StringPrinter::EscapeStyle escape_style);

// Mimic isprint() for Unicode codepoints.
static bool isprint32(char32_t codepoint) {
  if (codepoint <= 0x1F || codepoint == 0x7F) // C0
  {
    return false;
  }
  if (codepoint >= 0x80 && codepoint <= 0x9F) // C1
  {
    return false;
  }
  if (codepoint == 0x2028 || codepoint == 0x2029) // line/paragraph separators
  {
    return false;
  }
  if (codepoint == 0x200E || codepoint == 0x200F ||
      (codepoint >= 0x202A &&
       codepoint <= 0x202E)) // bidirectional text control
  {
    return false;
  }
  if (codepoint >= 0xFFF9 &&
      codepoint <= 0xFFFF) // interlinears and generally specials
  {
    return false;
  }
  return true;
}

DecodedCharBuffer attemptASCIIEscape(llvm::UTF32 c,
                                     StringPrinter::EscapeStyle escape_style) {
  const bool is_swift_escape_style =
      escape_style == StringPrinter::EscapeStyle::Swift;
  switch (c) {
  case 0:
    return {"\\0", 2};
  case '\a':
    return {"\\a", 2};
  case '\b':
    if (is_swift_escape_style)
      return nullptr;
    return {"\\b", 2};
  case '\f':
    if (is_swift_escape_style)
      return nullptr;
    return {"\\f", 2};
  case '\n':
    return {"\\n", 2};
  case '\r':
    return {"\\r", 2};
  case '\t':
    return {"\\t", 2};
  case '\v':
    if (is_swift_escape_style)
      return nullptr;
    return {"\\v", 2};
  case '\"':
    return {"\\\"", 2};
  case '\'':
    if (is_swift_escape_style)
      return {"\\'", 2};
    return nullptr;
  case '\\':
    return {"\\\\", 2};
  }
  return nullptr;
}

template <>
DecodedCharBuffer GetPrintableImpl<StringElementType::ASCII>(
    uint8_t *buffer, uint8_t *buffer_end, uint8_t *&next,
    StringPrinter::EscapeStyle escape_style) {
  // The ASCII helper always advances 1 byte at a time.
  next = buffer + 1;

  DecodedCharBuffer retval = attemptASCIIEscape(*buffer, escape_style);
  if (retval.GetSize())
    return retval;

  // Use llvm's locale-independent isPrint(char), instead of the libc
  // implementation which may give different results on different platforms.
  if (llvm::isPrint(*buffer))
    return {buffer, 1};

  unsigned escaped_len;
  constexpr unsigned max_buffer_size = 7;
  uint8_t data[max_buffer_size];
  switch (escape_style) {
  case StringPrinter::EscapeStyle::CXX:
    // Prints 4 characters, then a \0 terminator.
    escaped_len = snprintf((char *)data, max_buffer_size, "\\x%02x", *buffer);
    break;
  case StringPrinter::EscapeStyle::Swift:
    // Prints up to 6 characters, then a \0 terminator.
    escaped_len = snprintf((char *)data, max_buffer_size, "\\u{%x}", *buffer);
    break;
  }
  lldbassert(escaped_len > 0 && "unknown string escape style");
  return {data, escaped_len};
}

template <>
DecodedCharBuffer GetPrintableImpl<StringElementType::UTF8>(
    uint8_t *buffer, uint8_t *buffer_end, uint8_t *&next,
    StringPrinter::EscapeStyle escape_style) {
  // If the utf8 encoded length is invalid (i.e., not in the closed interval
  // [1;4]), or if there aren't enough bytes to print, or if the subsequence
  // isn't valid utf8, fall back to printing an ASCII-escaped subsequence.
  if (!llvm::isLegalUTF8Sequence(buffer, buffer_end))
    return GetPrintableImpl<StringElementType::ASCII>(buffer, buffer_end, next,
                                                      escape_style);

  // Convert the valid utf8 sequence to a utf32 codepoint. This cannot fail.
  llvm::UTF32 codepoint = 0;
  const llvm::UTF8 *buffer_for_conversion = buffer;
  llvm::ConversionResult result = llvm::convertUTF8Sequence(
      &buffer_for_conversion, buffer_end, &codepoint, llvm::strictConversion);
  assert(result == llvm::conversionOK &&
         "Failed to convert legal utf8 sequence");
  UNUSED_IF_ASSERT_DISABLED(result);

  // The UTF8 helper always advances by the utf8 encoded length.
  const unsigned utf8_encoded_len = buffer_for_conversion - buffer;
  next = buffer + utf8_encoded_len;

  DecodedCharBuffer retval = attemptASCIIEscape(codepoint, escape_style);
  if (retval.GetSize())
    return retval;
  if (isprint32(codepoint))
    return {buffer, utf8_encoded_len};

  unsigned escaped_len;
  constexpr unsigned max_buffer_size = 13;
  uint8_t data[max_buffer_size];
  switch (escape_style) {
  case StringPrinter::EscapeStyle::CXX:
    // Prints 10 characters, then a \0 terminator.
    escaped_len = snprintf((char *)data, max_buffer_size, "\\U%08x", codepoint);
    break;
  case StringPrinter::EscapeStyle::Swift:
    // Prints up to 12 characters, then a \0 terminator.
    escaped_len = snprintf((char *)data, max_buffer_size, "\\u{%x}", codepoint);
    break;
  }
  lldbassert(escaped_len > 0 && "unknown string escape style");
  return {data, escaped_len};
}

// Given a sequence of bytes, this function returns: a sequence of bytes to
// actually print out + a length the following unscanned position of the buffer
// is in next
static DecodedCharBuffer GetPrintable(StringElementType type, uint8_t *buffer,
                                      uint8_t *buffer_end, uint8_t *&next,
                                      StringPrinter::EscapeStyle escape_style) {
  if (!buffer || buffer >= buffer_end)
    return {nullptr};

  switch (type) {
  case StringElementType::ASCII:
    return GetPrintableImpl<StringElementType::ASCII>(buffer, buffer_end, next,
                                                      escape_style);
  case StringElementType::UTF8:
    return GetPrintableImpl<StringElementType::UTF8>(buffer, buffer_end, next,
                                                     escape_style);
  default:
    return {nullptr};
  }
}

static EscapingHelper
GetDefaultEscapingHelper(GetPrintableElementType elem_type,
                         StringPrinter::EscapeStyle escape_style) {
  switch (elem_type) {
  case GetPrintableElementType::UTF8:
  case GetPrintableElementType::ASCII:
    return [escape_style, elem_type](uint8_t *buffer, uint8_t *buffer_end,
                                     uint8_t *&next) -> DecodedCharBuffer {
      return GetPrintable(elem_type == GetPrintableElementType::UTF8
                              ? StringElementType::UTF8
                              : StringElementType::ASCII,
                          buffer, buffer_end, next, escape_style);
    };
  }
  llvm_unreachable("bad element type");
}

/// Read a string encoded in accordance with \tparam SourceDataType from a
/// host-side LLDB buffer, then pretty-print it to a stream using \p style.
template <typename SourceDataType>
static bool DumpEncodedBufferToStream(
    GetPrintableElementType style,
    llvm::ConversionResult (*ConvertFunction)(const SourceDataType **,
                                              const SourceDataType *,
                                              llvm::UTF8 **, llvm::UTF8 *,
                                              llvm::ConversionFlags),
    const StringPrinter::ReadBufferAndDumpToStreamOptions &dump_options) {
  assert(dump_options.GetStream() && "need a Stream to print the string to");
  Stream &stream(*dump_options.GetStream());
  if (dump_options.GetPrefixToken() != nullptr)
    stream.Printf("%s", dump_options.GetPrefixToken());
  if (dump_options.GetQuote() != 0)
    stream.Printf("%c", dump_options.GetQuote());
  auto data(dump_options.GetData());
  auto source_size(dump_options.GetSourceSize());
  if (data.GetByteSize() && data.GetDataStart() && data.GetDataEnd()) {
    const int bufferSPSize = data.GetByteSize();
    if (dump_options.GetSourceSize() == 0) {
      const int origin_encoding = 8 * sizeof(SourceDataType);
      source_size = bufferSPSize / (origin_encoding / 4);
    }

    const SourceDataType *data_ptr =
        (const SourceDataType *)data.GetDataStart();
    const SourceDataType *data_end_ptr = data_ptr + source_size;

    const bool zero_is_terminator = dump_options.GetBinaryZeroIsTerminator();

    if (zero_is_terminator) {
      while (data_ptr < data_end_ptr) {
        if (!*data_ptr) {
          data_end_ptr = data_ptr;
          break;
        }
        data_ptr++;
      }

      data_ptr = (const SourceDataType *)data.GetDataStart();
    }

    lldb::WritableDataBufferSP utf8_data_buffer_sp;
    llvm::UTF8 *utf8_data_ptr = nullptr;
    llvm::UTF8 *utf8_data_end_ptr = nullptr;

    if (ConvertFunction) {
      utf8_data_buffer_sp =
          std::make_shared<DataBufferHeap>(4 * bufferSPSize, 0);
      utf8_data_ptr = (llvm::UTF8 *)utf8_data_buffer_sp->GetBytes();
      utf8_data_end_ptr = utf8_data_ptr + utf8_data_buffer_sp->GetByteSize();
      ConvertFunction(&data_ptr, data_end_ptr, &utf8_data_ptr,
                      utf8_data_end_ptr, llvm::lenientConversion);
      if (!zero_is_terminator)
        utf8_data_end_ptr = utf8_data_ptr;
      // needed because the ConvertFunction will change the value of the
      // data_ptr.
      utf8_data_ptr =
          (llvm::UTF8 *)utf8_data_buffer_sp->GetBytes();
    } else {
      // just copy the pointers - the cast is necessary to make the compiler
      // happy but this should only happen if we are reading UTF8 data
      utf8_data_ptr = const_cast<llvm::UTF8 *>(
          reinterpret_cast<const llvm::UTF8 *>(data_ptr));
      utf8_data_end_ptr = const_cast<llvm::UTF8 *>(
          reinterpret_cast<const llvm::UTF8 *>(data_end_ptr));
    }

    const bool escape_non_printables = dump_options.GetEscapeNonPrintables();
    EscapingHelper escaping_callback;
    if (escape_non_printables)
      escaping_callback =
          GetDefaultEscapingHelper(style, dump_options.GetEscapeStyle());

    // since we tend to accept partial data (and even partially malformed data)
    // we might end up with no NULL terminator before the end_ptr hence we need
    // to take a slower route and ensure we stay within boundaries
    for (; utf8_data_ptr < utf8_data_end_ptr;) {
      if (zero_is_terminator && !*utf8_data_ptr)
        break;

      if (escape_non_printables) {
        uint8_t *next_data = nullptr;
        auto printable =
            escaping_callback(utf8_data_ptr, utf8_data_end_ptr, next_data);
        auto printable_bytes = printable.GetBytes();
        auto printable_size = printable.GetSize();

        // We failed to figure out how to print this string.
        if (!printable_bytes || !next_data)
          return false;

        for (unsigned c = 0; c < printable_size; c++)
          stream.Printf("%c", *(printable_bytes + c));
        utf8_data_ptr = (uint8_t *)next_data;
      } else {
        stream.Printf("%c", *utf8_data_ptr);
        utf8_data_ptr++;
      }
    }
  }
  if (dump_options.GetQuote() != 0)
    stream.Printf("%c", dump_options.GetQuote());
  if (dump_options.GetSuffixToken() != nullptr)
    stream.Printf("%s", dump_options.GetSuffixToken());
  if (dump_options.GetIsTruncated())
    stream.Printf("...");
  return true;
}

lldb_private::formatters::StringPrinter::ReadStringAndDumpToStreamOptions::
    ReadStringAndDumpToStreamOptions(ValueObject &valobj)
    : ReadStringAndDumpToStreamOptions() {
  SetEscapeNonPrintables(
      valobj.GetTargetSP()->GetDebugger().GetEscapeNonPrintables());
}

lldb_private::formatters::StringPrinter::ReadBufferAndDumpToStreamOptions::
    ReadBufferAndDumpToStreamOptions(ValueObject &valobj)
    : ReadBufferAndDumpToStreamOptions() {
  SetEscapeNonPrintables(
      valobj.GetTargetSP()->GetDebugger().GetEscapeNonPrintables());
}

lldb_private::formatters::StringPrinter::ReadBufferAndDumpToStreamOptions::
    ReadBufferAndDumpToStreamOptions(
        const ReadStringAndDumpToStreamOptions &options)
    : ReadBufferAndDumpToStreamOptions() {
  SetStream(options.GetStream());
  SetPrefixToken(options.GetPrefixToken());
  SetSuffixToken(options.GetSuffixToken());
  SetQuote(options.GetQuote());
  SetEscapeNonPrintables(options.GetEscapeNonPrintables());
  SetBinaryZeroIsTerminator(options.GetBinaryZeroIsTerminator());
  SetEscapeStyle(options.GetEscapeStyle());
}

namespace lldb_private {

namespace formatters {

template <typename SourceDataType>
static bool ReadEncodedBufferAndDumpToStream(
    StringElementType elem_type,
    const StringPrinter::ReadStringAndDumpToStreamOptions &options,
    llvm::ConversionResult (*ConvertFunction)(const SourceDataType **,
                                              const SourceDataType *,
                                              llvm::UTF8 **, llvm::UTF8 *,
                                              llvm::ConversionFlags)) {
  assert(options.GetStream() && "need a Stream to print the string to");
  if (!options.GetStream())
    return false;

  if (options.GetLocation() == 0 ||
      options.GetLocation() == LLDB_INVALID_ADDRESS)
    return false;

  lldb::TargetSP target_sp = options.GetTargetSP();
  if (!target_sp)
    return false;

  constexpr int type_width = sizeof(SourceDataType);
  constexpr int origin_encoding = 8 * type_width;
  if (origin_encoding != 8 && origin_encoding != 16 && origin_encoding != 32)
    return false;
  // If not UTF8 or ASCII, conversion to UTF8 is necessary.
  if (origin_encoding != 8 && !ConvertFunction)
    return false;

  bool needs_zero_terminator = options.GetNeedsZeroTermination();

  bool is_truncated = false;
  const auto max_size = target_sp->GetMaximumSizeOfStringSummary();

  uint32_t sourceSize;
  if (elem_type == StringElementType::ASCII && !options.GetSourceSize()) {
    // FIXME: The NSString formatter sets HasSourceSize(true) when the size is
    // actually unknown, as well as SetBinaryZeroIsTerminator(false). IIUC the
    // C++ formatter also sets SetBinaryZeroIsTerminator(false) when it doesn't
    // mean to. I don't see how this makes sense: we should fix the formatters.
    //
    // Until then, the behavior that's expected for ASCII strings with unknown
    // lengths is to read up to the max size and then null-terminate. Do that.
    sourceSize = max_size;
    needs_zero_terminator = true;
  } else if (options.HasSourceSize()) {
    sourceSize = options.GetSourceSize();
    if (!options.GetIgnoreMaxLength()) {
      if (sourceSize > max_size) {
        sourceSize = max_size;
        is_truncated = true;
      }
    }
  } else {
    sourceSize = max_size;
    needs_zero_terminator = true;
  }

  const int bufferSPSize = sourceSize * type_width;
  lldb::WritableDataBufferSP buffer_sp(new DataBufferHeap(bufferSPSize, 0));

  // Check if we got bytes. We never get any bytes if we have an empty
  // string, but we still continue so that we end up actually printing
  // an empty string ("").
  if (sourceSize != 0 && !buffer_sp->GetBytes())
    return false;

  Status error;
  char *buffer = reinterpret_cast<char *>(buffer_sp->GetBytes());

  if (elem_type == StringElementType::ASCII)
    target_sp->ReadCStringFromMemory(options.GetLocation(), buffer,
                                      bufferSPSize, error);
  else if (needs_zero_terminator)
    target_sp->ReadStringFromMemory(options.GetLocation(), buffer,
                                     bufferSPSize, error, type_width);
  else
    target_sp->ReadMemory(options.GetLocation(), buffer, bufferSPSize, error);
  if (error.Fail()) {
    options.GetStream()->Printf("unable to read data");
    return true;
  }

  StringPrinter::ReadBufferAndDumpToStreamOptions dump_options(options);
  dump_options.SetData(
      DataExtractor(buffer_sp, target_sp->GetArchitecture().GetByteOrder(),
                    target_sp->GetArchitecture().GetAddressByteSize()));
  dump_options.SetSourceSize(sourceSize);
  dump_options.SetIsTruncated(is_truncated);
  dump_options.SetNeedsZeroTermination(needs_zero_terminator);
  if (needs_zero_terminator)
    dump_options.SetBinaryZeroIsTerminator(true);

  GetPrintableElementType print_style = (elem_type == StringElementType::ASCII)
                                            ? GetPrintableElementType::ASCII
                                            : GetPrintableElementType::UTF8;
  return DumpEncodedBufferToStream(print_style, ConvertFunction, dump_options);
}

template <>
bool StringPrinter::ReadStringAndDumpToStream<StringElementType::UTF8>(
    const ReadStringAndDumpToStreamOptions &options) {
  return ReadEncodedBufferAndDumpToStream<llvm::UTF8>(StringElementType::UTF8,
                                                      options, nullptr);
}

template <>
bool StringPrinter::ReadStringAndDumpToStream<StringElementType::UTF16>(
    const ReadStringAndDumpToStreamOptions &options) {
  return ReadEncodedBufferAndDumpToStream<llvm::UTF16>(
      StringElementType::UTF16, options, llvm::ConvertUTF16toUTF8);
}

template <>
bool StringPrinter::ReadStringAndDumpToStream<StringElementType::UTF32>(
    const ReadStringAndDumpToStreamOptions &options) {
  return ReadEncodedBufferAndDumpToStream<llvm::UTF32>(
      StringElementType::UTF32, options, llvm::ConvertUTF32toUTF8);
}

template <>
bool StringPrinter::ReadStringAndDumpToStream<StringElementType::ASCII>(
    const ReadStringAndDumpToStreamOptions &options) {
  return ReadEncodedBufferAndDumpToStream<char>(StringElementType::ASCII,
                                                options, nullptr);
}

template <>
bool StringPrinter::ReadBufferAndDumpToStream<StringElementType::UTF8>(
    const ReadBufferAndDumpToStreamOptions &options) {
  return DumpEncodedBufferToStream<llvm::UTF8>(GetPrintableElementType::UTF8,
                                               nullptr, options);
}

template <>
bool StringPrinter::ReadBufferAndDumpToStream<StringElementType::UTF16>(
    const ReadBufferAndDumpToStreamOptions &options) {
  return DumpEncodedBufferToStream(GetPrintableElementType::UTF8,
                                   llvm::ConvertUTF16toUTF8, options);
}

template <>
bool StringPrinter::ReadBufferAndDumpToStream<StringElementType::UTF32>(
    const ReadBufferAndDumpToStreamOptions &options) {
  return DumpEncodedBufferToStream(GetPrintableElementType::UTF8,
                                   llvm::ConvertUTF32toUTF8, options);
}

template <>
bool StringPrinter::ReadBufferAndDumpToStream<StringElementType::ASCII>(
    const ReadBufferAndDumpToStreamOptions &options) {
  // Treat ASCII the same as UTF8.
  //
  // FIXME: This is probably not the right thing to do (well, it's debatable).
  // If an ASCII-encoded string happens to contain a sequence of invalid bytes
  // that forms a valid UTF8 character, we'll print out that character. This is
  // good if you're playing fast and loose with encodings (probably good for
  // std::string users), but maybe not so good if you care about your string
  // formatter respecting the semantics of your selected string encoding. In
  // the latter case you'd want to see the character byte sequence ('\x..'), not
  // the UTF8 character itself.
  return ReadBufferAndDumpToStream<StringElementType::UTF8>(options);
}

} // namespace formatters

} // namespace lldb_private
