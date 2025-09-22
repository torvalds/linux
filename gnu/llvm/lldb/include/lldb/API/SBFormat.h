//===-- SBFormat.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBFORMAT_H
#define LLDB_API_SBFORMAT_H

#include "lldb/API/SBDefines.h"

namespace lldb_private {
namespace python {
class SWIGBridge;
} // namespace python
namespace lua {
class SWIGBridge;
} // namespace lua
} // namespace lldb_private

namespace lldb {

/// Class that represents a format string that can be used to generate
/// descriptions of objects like frames and threads. See
/// https://lldb.llvm.org/use/formatting.html for more information.
class LLDB_API SBFormat {
public:
  SBFormat();

  /// Create an \a SBFormat by parsing the given format string. If parsing
  /// fails, this object is initialized as invalid.
  ///
  /// \param[in] format
  ///   The format string to parse.
  ///
  /// \param[out] error
  ///   An object where error messages will be written to if parsing fails.
  SBFormat(const char *format, lldb::SBError &error);

  SBFormat(const lldb::SBFormat &rhs);

  lldb::SBFormat &operator=(const lldb::SBFormat &rhs);

  ~SBFormat();

  /// \return
  ///   \b true if and only if this object is valid and can be used for
  ///   formatting.
  explicit operator bool() const;

protected:
  friend class SBFrame;
  friend class SBThread;

  /// \return
  ///   The underlying shared pointer storage for this object.
  lldb::FormatEntrySP GetFormatEntrySP() const;

  /// The storage for this object.
  lldb::FormatEntrySP m_opaque_sp;
};

} // namespace lldb
#endif // LLDB_API_SBFORMAT_H
