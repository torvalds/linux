//===-- LineTable.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_LINETABLE_H
#define LLDB_SYMBOL_LINETABLE_H

#include "lldb/Core/Address.h"
#include "lldb/Core/ModuleChild.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/SourceLocationSpec.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/lldb-private.h"
#include <vector>

namespace lldb_private {

/// \class LineSequence LineTable.h "lldb/Symbol/LineTable.h" An abstract base
/// class used during symbol table creation.
class LineSequence {
public:
  LineSequence();

  virtual ~LineSequence() = default;

  virtual void Clear() = 0;

private:
  LineSequence(const LineSequence &) = delete;
  const LineSequence &operator=(const LineSequence &) = delete;
};

/// \class LineTable LineTable.h "lldb/Symbol/LineTable.h"
/// A line table class.
class LineTable {
public:
  /// Construct with compile unit.
  ///
  /// \param[in] comp_unit
  ///     The compile unit to which this line table belongs.
  LineTable(CompileUnit *comp_unit);

  /// Construct with entries found in \a sequences.
  ///
  /// \param[in] sequences
  ///     Unsorted list of line sequences.
  LineTable(CompileUnit *comp_unit,
            std::vector<std::unique_ptr<LineSequence>> &&sequences);

  /// Destructor.
  ~LineTable();

  /// Adds a new line entry to this line table.
  ///
  /// All line entries are maintained in file address order.
  ///
  /// \param[in] line_entry
  ///     A const reference to a new line_entry to add to this line
  ///     table.
  ///
  /// \see Address::DumpStyle
  //  void
  //  AddLineEntry (const LineEntry& line_entry);

  // Called when you can't guarantee the addresses are in increasing order
  void InsertLineEntry(lldb::addr_t file_addr, uint32_t line, uint16_t column,
                       uint16_t file_idx, bool is_start_of_statement,
                       bool is_start_of_basic_block, bool is_prologue_end,
                       bool is_epilogue_begin, bool is_terminal_entry);

  // Used to instantiate the LineSequence helper class
  static std::unique_ptr<LineSequence> CreateLineSequenceContainer();

  // Append an entry to a caller-provided collection that will later be
  // inserted in this line table.
  static void AppendLineEntryToSequence(LineSequence *sequence, lldb::addr_t file_addr,
                                 uint32_t line, uint16_t column,
                                 uint16_t file_idx, bool is_start_of_statement,
                                 bool is_start_of_basic_block,
                                 bool is_prologue_end, bool is_epilogue_begin,
                                 bool is_terminal_entry);

  // Insert a sequence of entries into this line table.
  void InsertSequence(LineSequence *sequence);

  /// Dump all line entries in this line table to the stream \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// \param[in] style
  ///     The display style for the address.
  ///
  /// \see Address::DumpStyle
  void Dump(Stream *s, Target *target, Address::DumpStyle style,
            Address::DumpStyle fallback_style, bool show_line_ranges);

  void GetDescription(Stream *s, Target *target, lldb::DescriptionLevel level);

  /// Find a line entry that contains the section offset address \a so_addr.
  ///
  /// \param[in] so_addr
  ///     A section offset address object containing the address we
  ///     are searching for.
  ///
  /// \param[out] line_entry
  ///     A copy of the line entry that was found if \b true is
  ///     returned, otherwise \a entry is left unmodified.
  ///
  /// \param[out] index_ptr
  ///     A pointer to a 32 bit integer that will get the actual line
  ///     entry index if it is not nullptr.
  ///
  /// \return
  ///     Returns \b true if \a so_addr is contained in a line entry
  ///     in this line table, \b false otherwise.
  bool FindLineEntryByAddress(const Address &so_addr, LineEntry &line_entry,
                              uint32_t *index_ptr = nullptr);

  /// Find a line entry index that has a matching file index and source line
  /// number.
  ///
  /// Finds the next line entry that has a matching \a file_idx and source
  /// line number \a line starting at the \a start_idx entries into the line
  /// entry collection.
  ///
  /// \param[in] start_idx
  ///     The number of entries to skip when starting the search.
  ///
  /// \param[out] file_idx
  ///     The file index to search for that should be found prior
  ///     to calling this function using the following functions:
  ///     CompileUnit::GetSupportFiles()
  ///     FileSpecList::FindFileIndex (uint32_t, const FileSpec &) const
  ///
  /// \param[in] src_location_spec
  ///     The source location specifier to match.
  ///
  /// \param[out] line_entry_ptr
  ///     A pointer to a line entry object that will get a copy of
  ///     the line entry if \b true is returned, otherwise \a
  ///     line_entry is left untouched.
  ///
  /// \return
  ///     Returns \b true if a matching line entry is found in this
  ///     line table, \b false otherwise.
  ///
  /// \see CompileUnit::GetSupportFiles()
  /// \see FileSpecList::FindFileIndex (uint32_t, const FileSpec &) const
  uint32_t
  FindLineEntryIndexByFileIndex(uint32_t start_idx, uint32_t file_idx,
                                const SourceLocationSpec &src_location_spec,
                                LineEntry *line_entry_ptr);

  uint32_t FindLineEntryIndexByFileIndex(
      uint32_t start_idx, const std::vector<uint32_t> &file_idx,
      const SourceLocationSpec &src_location_spec, LineEntry *line_entry_ptr);

  size_t FindLineEntriesForFileIndex(uint32_t file_idx, bool append,
                                     SymbolContextList &sc_list);

  /// Get the line entry from the line table at index \a idx.
  ///
  /// \param[in] idx
  ///     An index into the line table entry collection.
  ///
  /// \return
  ///     A valid line entry if \a idx is a valid index, or an invalid
  ///     line entry if \a idx is not valid.
  ///
  /// \see LineTable::GetSize()
  /// \see LineEntry::IsValid() const
  bool GetLineEntryAtIndex(uint32_t idx, LineEntry &line_entry);

  /// Gets the size of the line table in number of line table entries.
  ///
  /// \return
  ///     The number of line table entries in this line table.
  uint32_t GetSize() const;

  typedef lldb_private::RangeVector<lldb::addr_t, lldb::addr_t, 32>
      FileAddressRanges;

  /// Gets all contiguous file address ranges for the entire line table.
  ///
  /// \param[out] file_ranges
  ///     A collection of file address ranges that will be filled in
  ///     by this function.
  ///
  /// \param[out] append
  ///     If \b true, then append to \a file_ranges, otherwise clear
  ///     \a file_ranges prior to adding any ranges.
  ///
  /// \return
  ///     The number of address ranges added to \a file_ranges
  size_t GetContiguousFileAddressRanges(FileAddressRanges &file_ranges,
                                        bool append);

  typedef RangeDataVector<lldb::addr_t, lldb::addr_t, lldb::addr_t>
      FileRangeMap;

  LineTable *LinkLineTable(const FileRangeMap &file_range_map);

  struct Entry {
    Entry()
        : line(0), is_start_of_statement(false), is_start_of_basic_block(false),
          is_prologue_end(false), is_epilogue_begin(false),
          is_terminal_entry(false) {}

    Entry(lldb::addr_t _file_addr, uint32_t _line, uint16_t _column,
          uint16_t _file_idx, bool _is_start_of_statement,
          bool _is_start_of_basic_block, bool _is_prologue_end,
          bool _is_epilogue_begin, bool _is_terminal_entry)
        : file_addr(_file_addr), line(_line),
          is_start_of_statement(_is_start_of_statement),
          is_start_of_basic_block(_is_start_of_basic_block),
          is_prologue_end(_is_prologue_end),
          is_epilogue_begin(_is_epilogue_begin),
          is_terminal_entry(_is_terminal_entry), column(_column),
          file_idx(_file_idx) {}

    int bsearch_compare(const void *key, const void *arrmem);

    void Clear() {
      file_addr = LLDB_INVALID_ADDRESS;
      line = 0;
      column = 0;
      file_idx = 0;
      is_start_of_statement = false;
      is_start_of_basic_block = false;
      is_prologue_end = false;
      is_epilogue_begin = false;
      is_terminal_entry = false;
    }

    static int Compare(const Entry &lhs, const Entry &rhs) {
// Compare the sections before calling
#define SCALAR_COMPARE(a, b)                                                   \
  if (a < b)                                                                   \
    return -1;                                                                 \
  if (a > b)                                                                   \
  return +1
      SCALAR_COMPARE(lhs.file_addr, rhs.file_addr);
      SCALAR_COMPARE(lhs.line, rhs.line);
      SCALAR_COMPARE(lhs.column, rhs.column);
      SCALAR_COMPARE(lhs.is_start_of_statement, rhs.is_start_of_statement);
      SCALAR_COMPARE(lhs.is_start_of_basic_block, rhs.is_start_of_basic_block);
      // rhs and lhs reversed on purpose below.
      SCALAR_COMPARE(rhs.is_prologue_end, lhs.is_prologue_end);
      SCALAR_COMPARE(lhs.is_epilogue_begin, rhs.is_epilogue_begin);
      // rhs and lhs reversed on purpose below.
      SCALAR_COMPARE(rhs.is_terminal_entry, lhs.is_terminal_entry);
      SCALAR_COMPARE(lhs.file_idx, rhs.file_idx);
#undef SCALAR_COMPARE
      return 0;
    }

    class LessThanBinaryPredicate {
    public:
      LessThanBinaryPredicate(LineTable *line_table);
      bool operator()(const LineTable::Entry &, const LineTable::Entry &) const;
      bool operator()(const std::unique_ptr<LineSequence> &,
                      const std::unique_ptr<LineSequence> &) const;

    protected:
      LineTable *m_line_table;
    };

    static bool EntryAddressLessThan(const Entry &lhs, const Entry &rhs) {
      return lhs.file_addr < rhs.file_addr;
    }

    // Member variables.
    /// The file address for this line entry.
    lldb::addr_t file_addr = LLDB_INVALID_ADDRESS;
    /// The source line number, or zero if there is no line number
    /// information.
    uint32_t line : 27;
    /// Indicates this entry is the beginning of a statement.
    uint32_t is_start_of_statement : 1;
    /// Indicates this entry is the beginning of a basic block.
    uint32_t is_start_of_basic_block : 1;
    /// Indicates this entry is one (of possibly many) where execution
    /// should be suspended for an entry breakpoint of a function.
    uint32_t is_prologue_end : 1;
    /// Indicates this entry is one (of possibly many) where execution
    /// should be suspended for an exit breakpoint of a function.
    uint32_t is_epilogue_begin : 1;
    /// Indicates this entry is that of the first byte after the end
    /// of a sequence of target machine instructions.
    uint32_t is_terminal_entry : 1;
    /// The column number of the source line, or zero if there is no
    /// column information.
    uint16_t column = 0;
    /// The file index into CompileUnit's file table, or zero if there
    /// is no file information.
    uint16_t file_idx = 0;
  };

protected:
  struct EntrySearchInfo {
    LineTable *line_table;
    lldb_private::Section *a_section;
    Entry *a_entry;
  };

  // Types
  typedef std::vector<lldb_private::Section *>
      section_collection; ///< The collection type for the sections.
  typedef std::vector<Entry>
      entry_collection; ///< The collection type for the line entries.
  // Member variables.
  CompileUnit
      *m_comp_unit; ///< The compile unit that this line table belongs to.
  entry_collection
      m_entries; ///< The collection of line entries in this line table.

  // Helper class
  class LineSequenceImpl : public LineSequence {
  public:
    LineSequenceImpl() = default;

    ~LineSequenceImpl() override = default;

    void Clear() override;

    entry_collection
        m_entries; ///< The collection of line entries in this sequence.
  };

  bool ConvertEntryAtIndexToLineEntry(uint32_t idx, LineEntry &line_entry);

private:
  LineTable(const LineTable &) = delete;
  const LineTable &operator=(const LineTable &) = delete;

  template <typename T>
  uint32_t FindLineEntryIndexByFileIndexImpl(
      uint32_t start_idx, T file_idx,
      const SourceLocationSpec &src_location_spec, LineEntry *line_entry_ptr,
      std::function<bool(T, uint16_t)> file_idx_matcher) {
    const size_t count = m_entries.size();
    size_t best_match = UINT32_MAX;

    if (!line_entry_ptr)
      return best_match;

    const uint32_t line = src_location_spec.GetLine().value_or(0);
    const uint16_t column =
        src_location_spec.GetColumn().value_or(LLDB_INVALID_COLUMN_NUMBER);
    const bool exact_match = src_location_spec.GetExactMatch();

    for (size_t idx = start_idx; idx < count; ++idx) {
      // Skip line table rows that terminate the previous row (is_terminal_entry
      // is non-zero)
      if (m_entries[idx].is_terminal_entry)
        continue;

      if (!file_idx_matcher(file_idx, m_entries[idx].file_idx))
        continue;

      // Exact match always wins.  Otherwise try to find the closest line > the
      // desired line.
      // FIXME: Maybe want to find the line closest before and the line closest
      // after and if they're not in the same function, don't return a match.

      if (column == LLDB_INVALID_COLUMN_NUMBER) {
        if (m_entries[idx].line < line) {
          continue;
        } else if (m_entries[idx].line == line) {
          ConvertEntryAtIndexToLineEntry(idx, *line_entry_ptr);
          return idx;
        } else if (!exact_match) {
          if (best_match == UINT32_MAX ||
              m_entries[idx].line < m_entries[best_match].line)
            best_match = idx;
        }
      } else {
        if (m_entries[idx].line < line) {
          continue;
        } else if (m_entries[idx].line == line &&
                   m_entries[idx].column == column) {
          ConvertEntryAtIndexToLineEntry(idx, *line_entry_ptr);
          return idx;
        } else if (!exact_match) {
          if (best_match == UINT32_MAX)
            best_match = idx;
          else if (m_entries[idx].line < m_entries[best_match].line)
            best_match = idx;
          else if (m_entries[idx].line == m_entries[best_match].line)
            if (m_entries[idx].column &&
                m_entries[idx].column < m_entries[best_match].column)
              best_match = idx;
        }
      }
    }

    if (best_match != UINT32_MAX) {
      if (line_entry_ptr)
        ConvertEntryAtIndexToLineEntry(best_match, *line_entry_ptr);
      return best_match;
    }
    return UINT32_MAX;
  }
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_LINETABLE_H
