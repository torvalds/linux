//===-- StringPrinter.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_STRINGPRINTER_H
#define LLDB_DATAFORMATTERS_STRINGPRINTER_H

#include <functional>
#include <string>

#include "lldb/Core/Address.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/lldb-forward.h"

namespace lldb_private {
namespace formatters {
class StringPrinter {
public:
  enum class StringElementType { ASCII, UTF8, UTF16, UTF32 };

  enum class GetPrintableElementType { ASCII, UTF8 };

  enum class EscapeStyle { CXX, Swift };

  class DumpToStreamOptions {
  public:
    DumpToStreamOptions() = default;

    void SetStream(Stream *s) { m_stream = s; }

    Stream *GetStream() const { return m_stream; }

    void SetPrefixToken(const std::string &p) { m_prefix_token = p; }

    void SetPrefixToken(std::nullptr_t) { m_prefix_token.clear(); }

    const char *GetPrefixToken() const { return m_prefix_token.c_str(); }

    void SetSuffixToken(const std::string &p) { m_suffix_token = p; }

    void SetSuffixToken(std::nullptr_t) { m_suffix_token.clear(); }

    const char *GetSuffixToken() const { return m_suffix_token.c_str(); }

    void SetQuote(char q) { m_quote = q; }

    char GetQuote() const { return m_quote; }

    void SetSourceSize(uint32_t s) { m_source_size = s; }

    uint32_t GetSourceSize() const { return m_source_size; }

    void SetNeedsZeroTermination(bool z) { m_needs_zero_termination = z; }

    bool GetNeedsZeroTermination() const { return m_needs_zero_termination; }

    void SetBinaryZeroIsTerminator(bool e) { m_zero_is_terminator = e; }

    bool GetBinaryZeroIsTerminator() const { return m_zero_is_terminator; }

    void SetEscapeNonPrintables(bool e) { m_escape_non_printables = e; }

    bool GetEscapeNonPrintables() const { return m_escape_non_printables; }

    void SetIgnoreMaxLength(bool e) { m_ignore_max_length = e; }

    bool GetIgnoreMaxLength() const { return m_ignore_max_length; }

    void SetEscapeStyle(EscapeStyle style) { m_escape_style = style; }

    EscapeStyle GetEscapeStyle() const { return m_escape_style; }

  private:
    /// The used output stream.
    Stream *m_stream = nullptr;
    /// String that should be printed before the heading quote character.
    std::string m_prefix_token;
    /// String that should be printed after the trailing quote character.
    std::string m_suffix_token;
    /// The quote character that should surround the string.
    char m_quote = '"';
    /// The length of the memory region that should be dumped in bytes.
    uint32_t m_source_size = 0;
    bool m_needs_zero_termination = true;
    /// True iff non-printable characters should be escaped when dumping
    /// them to the stream.
    bool m_escape_non_printables = true;
    /// True iff the max-string-summary-length setting of the target should
    /// be ignored.
    bool m_ignore_max_length = false;
    /// True iff a zero bytes ('\0') should terminate the memory region that
    /// is being dumped.
    bool m_zero_is_terminator = true;
    /// The language-specific style for escaping special characters.
    EscapeStyle m_escape_style = EscapeStyle::CXX;
  };

  class ReadStringAndDumpToStreamOptions : public DumpToStreamOptions {
  public:
    ReadStringAndDumpToStreamOptions() = default;

    ReadStringAndDumpToStreamOptions(ValueObject &valobj);

    void SetLocation(Address l) { m_location = std::move(l); }

    const Address &GetLocation() const { return m_location; }

    void SetTargetSP(lldb::TargetSP t) { m_target_sp = std::move(t); }

    lldb::TargetSP GetTargetSP() const { return m_target_sp; }

    void SetHasSourceSize(bool e) { m_has_source_size = e; }

    bool HasSourceSize() const { return m_has_source_size; }

  private:
    Address m_location;
    lldb::TargetSP m_target_sp;
    /// True iff we know the source size of the string.
    bool m_has_source_size = false;
  };

  class ReadBufferAndDumpToStreamOptions : public DumpToStreamOptions {
  public:
    ReadBufferAndDumpToStreamOptions() = default;

    ReadBufferAndDumpToStreamOptions(ValueObject &valobj);

    ReadBufferAndDumpToStreamOptions(
        const ReadStringAndDumpToStreamOptions &options);

    void SetData(DataExtractor &&d) { m_data = std::move(d); }

    const lldb_private::DataExtractor &GetData() const { return m_data; }

    void SetIsTruncated(bool t) { m_is_truncated = t; }

    bool GetIsTruncated() const { return m_is_truncated; }
  private:
    DataExtractor m_data;
    bool m_is_truncated = false;
  };

  template <StringElementType element_type>
  static bool
  ReadStringAndDumpToStream(const ReadStringAndDumpToStreamOptions &options);

  template <StringElementType element_type>
  static bool
  ReadBufferAndDumpToStream(const ReadBufferAndDumpToStreamOptions &options);
};

} // namespace formatters
} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_STRINGPRINTER_H
