//===-- DIERef.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DIEREF_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DIEREF_H

#include "lldb/Core/dwarf.h"
#include "lldb/Utility/LLDBAssert.h"
#include <cassert>
#include <optional>

namespace lldb_private::plugin {
namespace dwarf {
/// Identifies a DWARF debug info entry within a given Module. It contains three
/// "coordinates":
/// - file_index: identifies the separate stand alone debug info file
///   that is referred to by the main debug info file. This will be the
///   index of a DWO file for fission, or the .o file on mac when not
///   using a dSYM file. If this field is not set, then this references
///   a DIE inside the original object file.
/// - section: identifies the section of the debug info entry in the given file:
///   debug_info or debug_types.
/// - die_offset: The offset of the debug info entry as an absolute offset from
///   the beginning of the section specified in the section field.
class DIERef {
public:
  enum Section : uint8_t { DebugInfo, DebugTypes };
  DIERef(std::optional<uint32_t> file_index, Section section,
         dw_offset_t die_offset)
      : m_die_offset(die_offset), m_file_index(file_index.value_or(0)),
        m_file_index_valid(file_index ? true : false), m_section(section) {
    assert(this->file_index() == file_index && "File Index is out of range?");
  }

  explicit DIERef(lldb::user_id_t uid) {
    m_die_offset = uid & k_die_offset_mask;
    m_file_index_valid = (uid & k_file_index_valid_bit) != 0;
    m_file_index = m_file_index_valid
                       ? (uid >> k_die_offset_bit_size) & k_file_index_mask
                       : 0;
    m_section =
        (uid & k_section_bit) != 0 ? Section::DebugTypes : Section::DebugInfo;
  }

  lldb::user_id_t get_id() const {
    if (m_die_offset == k_die_offset_mask)
      return LLDB_INVALID_UID;

    return lldb::user_id_t(file_index().value_or(0)) << k_die_offset_bit_size |
           die_offset() | (m_file_index_valid ? k_file_index_valid_bit : 0) |
           (section() == Section::DebugTypes ? k_section_bit : 0);
  }

  std::optional<uint32_t> file_index() const {
    if (m_file_index_valid)
      return m_file_index;
    return std::nullopt;
  }

  Section section() const { return static_cast<Section>(m_section); }

  dw_offset_t die_offset() const { return m_die_offset; }

  bool operator<(DIERef other) const {
    if (m_file_index_valid != other.m_file_index_valid)
      return m_file_index_valid < other.m_file_index_valid;
    if (m_file_index_valid && (m_file_index != other.m_file_index))
      return m_file_index < other.m_file_index;
    if (m_section != other.m_section)
      return m_section < other.m_section;
    return m_die_offset < other.m_die_offset;
  }

  bool operator==(const DIERef &rhs) const {
    return file_index() == rhs.file_index() && m_section == rhs.m_section &&
           m_die_offset == rhs.m_die_offset;
  }

  bool operator!=(const DIERef &rhs) const { return !(*this == rhs); }

  /// Decode a serialized version of this object from data.
  ///
  /// \param data
  ///   The decoder object that references the serialized data.
  ///
  /// \param offset_ptr
  ///   A pointer that contains the offset from which the data will be decoded
  ///   from that gets updated as data gets decoded.
  ///
  /// \return
  ///   Returns a valid DIERef if decoding succeeded, std::nullopt if there was
  ///   unsufficient or invalid values that were decoded.
  static std::optional<DIERef> Decode(const DataExtractor &data,
                                      lldb::offset_t *offset_ptr);

  /// Encode this object into a data encoder object.
  ///
  /// This allows this object to be serialized to disk.
  ///
  /// \param encoder
  ///   A data encoder object that serialized bytes will be encoded into.
  ///
  void Encode(DataEncoder &encoder) const;

  static constexpr uint64_t k_die_offset_bit_size = DW_DIE_OFFSET_MAX_BITSIZE;
  static constexpr uint64_t k_file_index_bit_size =
      64 - DW_DIE_OFFSET_MAX_BITSIZE - /* size of control bits */ 2;

  static constexpr uint64_t k_file_index_valid_bit =
      (1ull << (k_file_index_bit_size + k_die_offset_bit_size));
  static constexpr uint64_t k_section_bit =
      (1ull << (k_file_index_bit_size + k_die_offset_bit_size + 1));
  static constexpr uint64_t
      k_file_index_mask = (~0ull) >> (64 - k_file_index_bit_size); // 0x3fffff;
  static constexpr uint64_t k_die_offset_mask = (~0ull) >>
                                                (64 - k_die_offset_bit_size);

private:
  // Allow 2TB of .debug_info/.debug_types offset
  dw_offset_t m_die_offset : k_die_offset_bit_size;
  // Used for DWO index or for .o file index on mac
  dw_offset_t m_file_index : k_file_index_bit_size;
  // Set to 1 if m_file_index is a DWO number
  dw_offset_t m_file_index_valid : 1;
  // Set to 0 for .debug_info 1 for .debug_types,
  dw_offset_t m_section : 1;
};
static_assert(sizeof(DIERef) == 8);

typedef std::vector<DIERef> DIEArray;
} // namespace dwarf
} // namespace lldb_private::plugin

namespace llvm {
template <> struct format_provider<lldb_private::plugin::dwarf::DIERef> {
  static void format(const lldb_private::plugin::dwarf::DIERef &ref,
                     raw_ostream &OS, StringRef Style);
};
} // namespace llvm

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DIEREF_H
