//===-- DWARFDeclContext.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFDeclContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace lldb_private::dwarf;
using namespace lldb_private::plugin::dwarf;

const char *DWARFDeclContext::Entry::GetName() const {
  if (name != nullptr)
    return name;
  if (tag == DW_TAG_namespace)
    return "(anonymous namespace)";
  if (tag == DW_TAG_class_type)
    return "(anonymous class)";
  if (tag == DW_TAG_structure_type)
    return "(anonymous struct)";
  if (tag == DW_TAG_union_type)
    return "(anonymous union)";
  return "(anonymous)";
}

const char *DWARFDeclContext::GetQualifiedName() const {
  if (m_qualified_name.empty()) {
    // The declaration context array for a class named "foo" in namespace
    // "a::b::c" will be something like:
    //  [0] DW_TAG_class_type "foo"
    //  [1] DW_TAG_namespace "c"
    //  [2] DW_TAG_namespace "b"
    //  [3] DW_TAG_namespace "a"
    if (!m_entries.empty()) {
      if (m_entries.size() == 1) {
        if (m_entries[0].name) {
          m_qualified_name.append("::");
          m_qualified_name.append(m_entries[0].name);
        }
      } else {
        llvm::raw_string_ostream string_stream(m_qualified_name);
        llvm::interleave(
            llvm::reverse(m_entries), string_stream,
            [&](auto entry) { string_stream << entry.GetName(); }, "::");
      }
    }
  }
  if (m_qualified_name.empty())
    return nullptr;
  return m_qualified_name.c_str();
}

bool DWARFDeclContext::operator==(const DWARFDeclContext &rhs) const {
  if (m_entries.size() != rhs.m_entries.size())
    return false;

  collection::const_iterator pos;
  collection::const_iterator begin = m_entries.begin();
  collection::const_iterator end = m_entries.end();

  collection::const_iterator rhs_pos;
  collection::const_iterator rhs_begin = rhs.m_entries.begin();
  // The two entry arrays have the same size

  // First compare the tags before we do expensive name compares
  for (pos = begin, rhs_pos = rhs_begin; pos != end; ++pos, ++rhs_pos) {
    if (pos->tag != rhs_pos->tag) {
      // Check for DW_TAG_structure_type and DW_TAG_class_type as they are
      // often used interchangeably in GCC
      if (pos->tag == DW_TAG_structure_type &&
          rhs_pos->tag == DW_TAG_class_type)
        continue;
      if (pos->tag == DW_TAG_class_type &&
          rhs_pos->tag == DW_TAG_structure_type)
        continue;
      return false;
    }
  }
  // The tags all match, now compare the names
  for (pos = begin, rhs_pos = rhs_begin; pos != end; ++pos, ++rhs_pos) {
    if (!pos->NameMatches(*rhs_pos))
      return false;
  }
  // All tags and names match
  return true;
}
