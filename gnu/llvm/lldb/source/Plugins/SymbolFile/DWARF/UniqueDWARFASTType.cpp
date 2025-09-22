//===-- UniqueDWARFASTType.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UniqueDWARFASTType.h"

#include "lldb/Core/Declaration.h"

using namespace lldb_private::dwarf;
using namespace lldb_private::plugin::dwarf;

static bool IsStructOrClassTag(llvm::dwarf::Tag Tag) {
  return Tag == llvm::dwarf::Tag::DW_TAG_class_type ||
         Tag == llvm::dwarf::Tag::DW_TAG_structure_type;
}

UniqueDWARFASTType *UniqueDWARFASTTypeList::Find(
    const DWARFDIE &die, const lldb_private::Declaration &decl,
    const int32_t byte_size, bool is_forward_declaration) {
  for (UniqueDWARFASTType &udt : m_collection) {
    // Make sure the tags match
    if (udt.m_die.Tag() == die.Tag() || (IsStructOrClassTag(udt.m_die.Tag()) &&
                                         IsStructOrClassTag(die.Tag()))) {
      // If they are not both definition DIEs or both declaration DIEs, then
      // don't check for byte size and declaration location, because declaration
      // DIEs usually don't have those info.
      bool matching_size_declaration =
          udt.m_is_forward_declaration != is_forward_declaration
              ? true
              : (udt.m_byte_size < 0 || byte_size < 0 ||
                 udt.m_byte_size == byte_size) &&
                    udt.m_declaration == decl;
      if (!matching_size_declaration)
        continue;
      // The type has the same name, and was defined on the same file and
      // line. Now verify all of the parent DIEs match.
      DWARFDIE parent_arg_die = die.GetParent();
      DWARFDIE parent_pos_die = udt.m_die.GetParent();
      bool match = true;
      bool done = false;
      while (!done && match && parent_arg_die && parent_pos_die) {
        const dw_tag_t parent_arg_tag = parent_arg_die.Tag();
        const dw_tag_t parent_pos_tag = parent_pos_die.Tag();
        if (parent_arg_tag == parent_pos_tag ||
            (IsStructOrClassTag(parent_arg_tag) &&
             IsStructOrClassTag(parent_pos_tag))) {
          switch (parent_arg_tag) {
          case DW_TAG_class_type:
          case DW_TAG_structure_type:
          case DW_TAG_union_type:
          case DW_TAG_namespace: {
            const char *parent_arg_die_name = parent_arg_die.GetName();
            if (parent_arg_die_name == nullptr) {
              // Anonymous (i.e. no-name) struct
              match = false;
            } else {
              const char *parent_pos_die_name = parent_pos_die.GetName();
              if (parent_pos_die_name == nullptr ||
                  ((parent_arg_die_name != parent_pos_die_name) &&
                   strcmp(parent_arg_die_name, parent_pos_die_name)))
                match = false;
            }
          } break;

          case DW_TAG_compile_unit:
          case DW_TAG_partial_unit:
            done = true;
            break;
          default:
            break;
          }
        }
        parent_arg_die = parent_arg_die.GetParent();
        parent_pos_die = parent_pos_die.GetParent();
      }

      if (match) {
        return &udt;
      }
    }
  }
  return nullptr;
}
