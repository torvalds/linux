//===-- Declaration.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_DECLARATION_H
#define LLDB_SYMBOL_DECLARATION_H

#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

/// \class Declaration Declaration.h "lldb/Core/Declaration.h"
/// A class that describes the declaration location of a
///        lldb object.
///
/// The declarations include the file specification, line number, and the
/// column info and can help track where functions, blocks, inlined functions,
/// types, variables, any many other debug core objects were declared.
class Declaration {
public:
  /// Default constructor.
  Declaration() = default;

  /// Construct with file specification, and optional line and column.
  ///
  /// \param[in] file_spec
  ///     The file specification that describes where this was
  ///     declared.
  ///
  /// \param[in] line
  ///     The line number that describes where this was declared. Set
  ///     to zero if there is no line number information.
  ///
  /// \param[in] column
  ///     The column number that describes where this was declared.
  ///     Set to zero if there is no column number information.
  Declaration(const FileSpec &file_spec, uint32_t line = 0,
              uint16_t column = LLDB_INVALID_COLUMN_NUMBER)
      : m_file(file_spec), m_line(line), m_column(column) {}

  /// Construct with a pointer to another Declaration object.
  Declaration(const Declaration *decl_ptr)
      : m_line(0), m_column(LLDB_INVALID_COLUMN_NUMBER) {
    if (decl_ptr)
      *this = *decl_ptr;
  }

  /// Clear the object's state.
  ///
  /// Sets the file specification to be empty, and the line and column to
  /// zero.
  void Clear() {
    m_file.Clear();
    m_line = 0;
    m_column = 0;
  }

  /// Compare two declaration objects.
  ///
  /// Compares the two file specifications from \a lhs and \a rhs. If the file
  /// specifications are equal, then continue to compare the line number and
  /// column numbers respectively.
  ///
  /// \param[in] lhs
  ///     The Left Hand Side const Declaration object reference.
  ///
  /// \param[in] rhs
  ///     The Right Hand Side const Declaration object reference.
  ///
  /// \return
  ///     -1 if lhs < rhs
  ///     0 if lhs == rhs
  ///     1 if lhs > rhs
  static int Compare(const Declaration &lhs, const Declaration &rhs);

  /// Checks if this object has the same file and line as another declaration
  /// object.
  ///
  /// \param[in] declaration
  ///     The const Declaration object to compare with.
  ///
  /// \return
  ///     Returns \b true if \b declaration is at the same file and
  ///     line, \b false otherwise.
  bool FileAndLineEqual(const Declaration &declaration) const;

  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  void Dump(Stream *s, bool show_fullpaths) const;

  bool DumpStopContext(Stream *s, bool show_fullpaths) const;

  /// Get accessor for file specification.
  ///
  /// \return
  ///     A reference to the file specification object.
  FileSpec &GetFile() { return m_file; }

  /// Get const accessor for file specification.
  ///
  /// \return
  ///     A const reference to the file specification object.
  const FileSpec &GetFile() const { return m_file; }

  /// Get accessor for the declaration line number.
  ///
  /// \return
  ///     Non-zero indicates a valid line number, zero indicates no
  ///     line information is available.
  uint32_t GetLine() const { return m_line; }

  /// Get accessor for the declaration column number.
  ///
  /// \return
  ///     Non-zero indicates a valid column number, zero indicates no
  ///     column information is available.
  uint16_t GetColumn() const { return m_column; }

  /// Convert to boolean operator.
  ///
  /// This allows code to check a Declaration object to see if it
  /// contains anything valid using code such as:
  ///
  /// \code
  /// Declaration decl(...);
  /// if (decl)
  /// { ...
  /// \endcode
  ///
  /// \return
  ///     A \b true if both the file_spec and the line are valid,
  ///     \b false otherwise.
  explicit operator bool() const { return IsValid(); }

  bool IsValid() const {
    return m_file && m_line != 0 && m_line != LLDB_INVALID_LINE_NUMBER;
  }

  /// Get the memory cost of this object.
  ///
  /// \return
  ///     The number of bytes that this object occupies in memory.
  ///     The returned value does not include the bytes for any
  ///     shared string values.
  size_t MemorySize() const;

  /// Set accessor for the declaration file specification.
  ///
  /// \param[in] file_spec
  ///     The new declaration file specification.
  void SetFile(const FileSpec &file_spec) { m_file = file_spec; }

  /// Set accessor for the declaration line number.
  ///
  /// \param[in] line
  ///     Non-zero indicates a valid line number, zero indicates no
  ///     line information is available.
  void SetLine(uint32_t line) { m_line = line; }

  /// Set accessor for the declaration column number.
  ///
  /// \param[in] column
  ///     Non-zero indicates a valid column number, zero indicates no
  ///     column information is available.
  void SetColumn(uint16_t column) { m_column = column; }

protected:
  /// The file specification that points to the source file where the
  /// declaration occurred.
  FileSpec m_file;
  /// Non-zero values indicates a valid line number, zero indicates no line
  /// number information is available.
  uint32_t m_line = 0;
  /// Non-zero values indicates a valid column number, zero indicates no column
  /// information is available.
  uint16_t m_column = LLDB_INVALID_COLUMN_NUMBER;
};

bool operator==(const Declaration &lhs, const Declaration &rhs);

} // namespace lldb_private

#endif // LLDB_SYMBOL_DECLARATION_H
