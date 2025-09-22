//===-- DWARFDebugInfoEntry.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEBUGINFOENTRY_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEBUGINFOENTRY_H

#include "SymbolFileDWARF.h"
#include "llvm/ADT/SmallVector.h"

#include "DWARFAttribute.h"
#include "DWARFBaseDIE.h"
#include "DWARFDebugRanges.h"
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"

namespace lldb_private::plugin {
namespace dwarf {
class DWARFDeclContext;

#define DIE_SIBLING_IDX_BITSIZE 31

/// DWARFDebugInfoEntry objects assume that they are living in one big
/// vector and do pointer arithmetic on their this pointers. Don't
/// pass them by value. Due to the way they are constructed in a
/// std::vector, we cannot delete the copy constructor.
class DWARFDebugInfoEntry {
public:
  typedef std::vector<DWARFDebugInfoEntry> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  DWARFDebugInfoEntry()
      : m_offset(DW_INVALID_OFFSET), m_parent_idx(0), m_sibling_idx(0),
        m_has_children(false) {}

  explicit operator bool() const { return m_offset != DW_INVALID_OFFSET; }
  bool operator==(const DWARFDebugInfoEntry &rhs) const;
  bool operator!=(const DWARFDebugInfoEntry &rhs) const;

  void BuildFunctionAddressRangeTable(DWARFUnit *cu,
                                      DWARFDebugAranges *debug_aranges) const;

  bool Extract(const DWARFDataExtractor &data, const DWARFUnit &cu,
               lldb::offset_t *offset_ptr);

  using Recurse = DWARFBaseDIE::Recurse;
  DWARFAttributes GetAttributes(DWARFUnit *cu,
                                Recurse recurse = Recurse::yes) const {
    DWARFAttributes attrs;
    GetAttributes(cu, attrs, recurse, 0 /* curr_depth */);
    return attrs;
  }

  dw_offset_t
  GetAttributeValue(const DWARFUnit *cu, const dw_attr_t attr,
                    DWARFFormValue &formValue,
                    dw_offset_t *end_attr_offset_ptr = nullptr,
                    bool check_specification_or_abstract_origin = false) const;

  const char *GetAttributeValueAsString(
      const DWARFUnit *cu, const dw_attr_t attr, const char *fail_value,
      bool check_specification_or_abstract_origin = false) const;

  uint64_t GetAttributeValueAsUnsigned(
      const DWARFUnit *cu, const dw_attr_t attr, uint64_t fail_value,
      bool check_specification_or_abstract_origin = false) const;

  std::optional<uint64_t> GetAttributeValueAsOptionalUnsigned(
      const DWARFUnit *cu, const dw_attr_t attr,
      bool check_specification_or_abstract_origin = false) const;

  DWARFDIE GetAttributeValueAsReference(
      const DWARFUnit *cu, const dw_attr_t attr,
      bool check_specification_or_abstract_origin = false) const;

  uint64_t GetAttributeValueAsAddress(
      const DWARFUnit *cu, const dw_attr_t attr, uint64_t fail_value,
      bool check_specification_or_abstract_origin = false) const;

  dw_addr_t
  GetAttributeHighPC(const DWARFUnit *cu, dw_addr_t lo_pc, uint64_t fail_value,
                     bool check_specification_or_abstract_origin = false) const;

  bool GetAttributeAddressRange(
      const DWARFUnit *cu, dw_addr_t &lo_pc, dw_addr_t &hi_pc,
      uint64_t fail_value,
      bool check_specification_or_abstract_origin = false) const;

  DWARFRangeList GetAttributeAddressRanges(
      DWARFUnit *cu, bool check_hi_lo_pc,
      bool check_specification_or_abstract_origin = false) const;

  const char *GetName(const DWARFUnit *cu) const;

  const char *GetMangledName(const DWARFUnit *cu,
                             bool substitute_name_allowed = true) const;

  const char *GetPubname(const DWARFUnit *cu) const;

  bool GetDIENamesAndRanges(DWARFUnit *cu, const char *&name,
                            const char *&mangled, DWARFRangeList &rangeList,
                            std::optional<int> &decl_file,
                            std::optional<int> &decl_line,
                            std::optional<int> &decl_column,
                            std::optional<int> &call_file,
                            std::optional<int> &call_line,
                            std::optional<int> &call_column,
                            DWARFExpressionList *frame_base = nullptr) const;

  const llvm::DWARFAbbreviationDeclaration *
  GetAbbreviationDeclarationPtr(const DWARFUnit *cu) const;

  lldb::offset_t GetFirstAttributeOffset() const;

  dw_tag_t Tag() const { return m_tag; }

  bool IsNULL() const { return m_abbr_idx == 0; }

  dw_offset_t GetOffset() const { return m_offset; }

  bool HasChildren() const { return m_has_children; }

  void SetHasChildren(bool b) { m_has_children = b; }

  // We know we are kept in a vector of contiguous entries, so we know
  // our parent will be some index behind "this".
  DWARFDebugInfoEntry *GetParent() {
    return m_parent_idx > 0 ? this - m_parent_idx : nullptr;
  }
  const DWARFDebugInfoEntry *GetParent() const {
    return m_parent_idx > 0 ? this - m_parent_idx : nullptr;
  }
  // We know we are kept in a vector of contiguous entries, so we know
  // our sibling will be some index after "this".
  DWARFDebugInfoEntry *GetSibling() {
    return m_sibling_idx > 0 ? this + m_sibling_idx : nullptr;
  }
  const DWARFDebugInfoEntry *GetSibling() const {
    return m_sibling_idx > 0 ? this + m_sibling_idx : nullptr;
  }
  // We know we are kept in a vector of contiguous entries, so we know
  // we don't need to store our child pointer, if we have a child it will
  // be the next entry in the list...
  DWARFDebugInfoEntry *GetFirstChild() {
    return HasChildren() ? this + 1 : nullptr;
  }
  const DWARFDebugInfoEntry *GetFirstChild() const {
    return HasChildren() ? this + 1 : nullptr;
  }

  void SetSiblingIndex(uint32_t idx) { m_sibling_idx = idx; }
  void SetParentIndex(uint32_t idx) { m_parent_idx = idx; }

  // This function returns true if the variable scope is either
  // global or (file-static). It will return false for static variables
  // that are local to a function, as they have local scope.
  bool IsGlobalOrStaticScopeVariable() const;

protected:
  // Up to 2TB offset within the .debug_info/.debug_types
  dw_offset_t m_offset : DW_DIE_OFFSET_MAX_BITSIZE;
  // How many to subtract from "this" to get the parent. If zero this die has no
  // parent
  dw_offset_t m_parent_idx : 64 - DW_DIE_OFFSET_MAX_BITSIZE;
  // How many to add to "this" to get the sibling.
  // If it is zero, then the DIE doesn't have children,
  // or the DWARF claimed it had children but the DIE
  // only contained a single NULL terminating child.
  uint32_t m_sibling_idx : 31, m_has_children : 1;
  uint16_t m_abbr_idx = 0;
  /// A copy of the DW_TAG value so we don't have to go through the compile
  /// unit abbrev table
  dw_tag_t m_tag = llvm::dwarf::DW_TAG_null;

private:
  void GetAttributes(DWARFUnit *cu, DWARFAttributes &attrs, Recurse recurse,
                     uint32_t curr_depth) const;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEBUGINFOENTRY_H
