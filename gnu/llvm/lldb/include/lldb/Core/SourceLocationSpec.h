//===-- SourceLocationSpec.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_SOURCELOCATIONSPEC_H
#define LLDB_UTILITY_SOURCELOCATIONSPEC_H

#include "lldb/Core/Declaration.h"
#include "lldb/lldb-defines.h"

#include <optional>
#include <string>

namespace lldb_private {

/// \class SourceLocationSpec SourceLocationSpec.h
/// "lldb/Core/SourceLocationSpec.h" A source location specifier class.
///
/// A source location specifier class that holds a Declaration object containing
/// a FileSpec with line and column information. The column line is optional.
/// It also holds search flags that can be fetched by resolvers to look inlined
/// declarations and/or exact matches.
class SourceLocationSpec {
public:
  /// Constructor.
  ///
  /// Takes a \a file_spec with a \a line number and a \a column number. If
  /// \a column is null or not provided, it is set to std::nullopt.
  ///
  /// \param[in] file_spec
  ///     The full or partial path to a file.
  ///
  /// \param[in] line
  ///     The line number in the source file.
  ///
  ///  \param[in] column
  ///     The column number in the line of the source file.
  ///
  ///  \param[in] check_inlines
  ///     Whether to look for a match in inlined declaration.
  ///
  ///  \param[in] exact_match
  ///     Whether to look for an exact match.
  ///
  explicit SourceLocationSpec(FileSpec file_spec, uint32_t line,
                              std::optional<uint16_t> column = std::nullopt,
                              bool check_inlines = false,
                              bool exact_match = false);

  SourceLocationSpec() = delete;

  /// Convert to boolean operator.
  ///
  /// This allows code to check a SourceLocationSpec object to see if it
  /// contains anything valid using code such as:
  ///
  /// \code
  /// SourceLocationSpec location_spec(...);
  /// if (location_spec)
  /// { ...
  /// \endcode
  ///
  /// \return
  ///     A pointer to this object if both the file_spec and the line are valid,
  ///     nullptr otherwise.
  explicit operator bool() const;

  /// Logical NOT operator.
  ///
  /// This allows code to check a SourceLocationSpec object to see if it is
  /// invalid using code such as:
  ///
  /// \code
  /// SourceLocationSpec location_spec(...);
  /// if (!location_spec)
  /// { ...
  /// \endcode
  ///
  /// \return
  ///     Returns \b true if the object has an invalid file_spec or line number,
  ///     \b false otherwise.
  bool operator!() const;

  /// Equal to operator
  ///
  /// Tests if this object is equal to \a rhs.
  ///
  /// \param[in] rhs
  ///     A const SourceLocationSpec object reference to compare this object
  ///     to.
  ///
  /// \return
  ///     \b true if this object is equal to \a rhs, \b false
  ///     otherwise.
  bool operator==(const SourceLocationSpec &rhs) const;

  /// Not equal to operator
  ///
  /// Tests if this object is not equal to \a rhs.
  ///
  /// \param[in] rhs
  ///     A const SourceLocationSpec object reference to compare this object
  ///     to.
  ///
  /// \return
  ///     \b true if this object is equal to \a rhs, \b false
  ///     otherwise.
  bool operator!=(const SourceLocationSpec &rhs) const;

  /// Less than to operator
  ///
  /// Tests if this object is less than \a rhs.
  ///
  /// \param[in] rhs
  ///     A const SourceLocationSpec object reference to compare this object
  ///     to.
  ///
  /// \return
  ///     \b true if this object is less than \a rhs, \b false
  ///     otherwise.
  bool operator<(const SourceLocationSpec &rhs) const;

  /// Compare two SourceLocationSpec objects.
  ///
  /// If \a full is true, then the file_spec, the line and column must match.
  /// If \a full is false, then only the file_spec and line number for \a lhs
  /// and \a rhs are compared. This allows a SourceLocationSpec object that have
  /// no column information to match a  SourceLocationSpec objects that have
  /// column information with matching file_spec and line component.
  ///
  /// \param[in] lhs
  ///     A const reference to the Left Hand Side object to compare.
  ///
  /// \param[in] rhs
  ///     A const reference to the Right Hand Side object to compare.
  ///
  /// \param[in] full
  ///     If true, then the file_spec, the line and column must match for a
  ///     compare to return zero (equal to). If false, then only the file_spec
  ///     and line number for \a lhs and \a rhs are compared, else a full
  ///     comparison is done.
  ///
  /// \return -1 if \a lhs is less than \a rhs, 0 if \a lhs is equal to \a rhs,
  ///     1 if \a lhs is greater than \a rhs
  static int Compare(const SourceLocationSpec &lhs,
                     const SourceLocationSpec &rhs);

  static bool Equal(const SourceLocationSpec &lhs,
                    const SourceLocationSpec &rhs, bool full);

  /// Dump this object to a Stream.
  ///
  /// Dump the object to the supplied stream \a s, starting with the file name,
  /// then the line number and if available the column number.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  void Dump(Stream &s) const;

  std::string GetString() const;

  FileSpec GetFileSpec() const { return m_declaration.GetFile(); }

  std::optional<uint32_t> GetLine() const;

  std::optional<uint16_t> GetColumn() const;

  bool GetCheckInlines() const { return m_check_inlines; }

  bool GetExactMatch() const { return m_exact_match; }

protected:
  Declaration m_declaration;
  /// Tells if the resolver should look in inlined declaration.
  bool m_check_inlines;
  /// Tells if the resolver should look for an exact match.
  bool m_exact_match;
};

/// Dump a SourceLocationSpec object to a stream
Stream &operator<<(Stream &s, const SourceLocationSpec &loc);
} // namespace lldb_private

#endif // LLDB_UTILITY_SOURCELOCATIONSPEC_H
