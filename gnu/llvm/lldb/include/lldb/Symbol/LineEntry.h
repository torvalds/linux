//===-- LineEntry.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_LINEENTRY_H
#define LLDB_SYMBOL_LINEENTRY_H

#include "lldb/Core/AddressRange.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/SupportFile.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

/// \class LineEntry LineEntry.h "lldb/Symbol/LineEntry.h"
/// A line table entry class.
struct LineEntry {
  /// Default constructor.
  ///
  /// Initialize all member variables to invalid values.
  LineEntry();

  /// Clear the object's state.
  ///
  /// Clears all member variables to invalid values.
  void Clear();

  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// \param[in] show_file
  ///     If \b true, display the filename with the line entry which
  ///     requires that the compile unit object \a comp_unit be a
  ///     valid pointer.
  ///
  /// \param[in] style
  ///     The display style for the section offset address.
  ///
  /// \return
  ///     Returns \b true if the address was able to be displayed
  ///     using \a style. File and load addresses may be unresolved
  ///     and it may not be possible to display a valid address value.
  ///     Returns \b false if the address was not able to be properly
  ///     dumped.
  ///
  /// \see Address::DumpStyle
  bool Dump(Stream *s, Target *target, bool show_file, Address::DumpStyle style,
            Address::DumpStyle fallback_style, bool show_range) const;

  bool GetDescription(Stream *s, lldb::DescriptionLevel level, CompileUnit *cu,
                      Target *target, bool show_address_only) const;

  /// Dumps information specific to a process that stops at this line entry to
  /// the supplied stream \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// \return
  ///     Returns \b true if the file and line were properly dumped,
  ///     \b false otherwise.
  bool DumpStopContext(Stream *s, bool show_fullpaths) const;

  /// Check if a line entry object is valid.
  ///
  /// \return
  ///     Returns \b true if the line entry contains a valid section
  ///     offset address, file index, and line number, \b false
  ///     otherwise.
  bool IsValid() const;

  /// Compare two LineEntry objects.
  ///
  /// \param[in] lhs
  ///     The Left Hand Side const LineEntry object reference.
  ///
  /// \param[in] rhs
  ///     The Right Hand Side const LineEntry object reference.
  ///
  /// \return
  ///     -1 if lhs < rhs
  ///     0 if lhs == rhs
  ///     1 if lhs > rhs
  static int Compare(const LineEntry &lhs, const LineEntry &rhs);

  /// Give the range for this LineEntry + any additional LineEntries for this
  /// same source line that are contiguous.
  ///
  /// A compiler may emit multiple line entries for a single source line,
  /// e.g. to indicate subexpressions at different columns.  This method will
  /// get the AddressRange for all of the LineEntries for this source line
  /// that are contiguous.
  //
  /// Line entries with a line number of 0 are treated specially - these are
  /// compiler-generated line table entries that the user did not write in
  /// their source code, and we want to skip past in the debugger. If this
  /// LineEntry is for line 32, and the following LineEntry is for line 0, we
  /// will extend the range to include the AddressRange of the line 0
  /// LineEntry (and it will include the range of the following LineEntries
  /// that match either 32 or 0.)
  ///
  /// When \b include_inlined_functions is \b true inlined functions with
  /// a call site at this LineEntry will also be included in the complete
  /// range.
  ///
  /// If the initial LineEntry this method is called on is a line #0, only the
  /// range of continuous LineEntries with line #0 will be included in the
  /// complete range.
  ///
  /// @param[in] include_inlined_functions
  ///     Whether to include inlined functions at the same line or not.
  ///
  /// \return
  ///     The contiguous AddressRange for this source line.
  AddressRange
  GetSameLineContiguousAddressRange(bool include_inlined_functions) const;

  /// Apply file mappings from target.source-map to the LineEntry's file.
  ///
  /// \param[in] target_sp
  ///     Shared pointer to the target this LineEntry belongs to.
  void ApplyFileMappings(lldb::TargetSP target_sp);

  /// Helper to access the file.
  const FileSpec &GetFile() const { return file_sp->GetSpecOnly(); }

  /// The section offset address range for this line entry.
  AddressRange range;

  /// The source file, possibly mapped by the target.source-map setting.
  lldb::SupportFileSP file_sp;

  /// The original source file, from debug info.
  lldb::SupportFileSP original_file_sp;

  /// The source line number, or LLDB_INVALID_LINE_NUMBER if there is no line
  /// number information.
  uint32_t line = LLDB_INVALID_LINE_NUMBER;

  /// The column number of the source line, or zero if there is no column
  /// information.
  uint16_t column = 0;

  /// Indicates this entry is the beginning of a statement.
  uint16_t is_start_of_statement : 1;

  /// Indicates this entry is the beginning of a basic block.
  uint16_t is_start_of_basic_block : 1;

  /// Indicates this entry is one (of possibly many) where execution should be
  /// suspended for an entry breakpoint of a function.
  uint16_t is_prologue_end : 1;

  /// Indicates this entry is one (of possibly many) where execution should be
  /// suspended for an exit breakpoint of a function.
  uint16_t is_epilogue_begin : 1;

  /// Indicates this entry is that of the first byte after the end of a sequence
  /// of target machine instructions.
  uint16_t is_terminal_entry : 1;
};

/// Less than operator.
///
/// \param[in] lhs
///     The Left Hand Side const LineEntry object reference.
///
/// \param[in] rhs
///     The Right Hand Side const LineEntry object reference.
///
/// \return
///     Returns \b true if lhs < rhs, false otherwise.
bool operator<(const LineEntry &lhs, const LineEntry &rhs);

} // namespace lldb_private

#endif // LLDB_SYMBOL_LINEENTRY_H
