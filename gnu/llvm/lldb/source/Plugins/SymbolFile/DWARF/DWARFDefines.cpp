//===-- DWARFDefines.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFDefines.h"
#include "lldb/Utility/ConstString.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace lldb_private::plugin {
namespace dwarf {

llvm::StringRef DW_TAG_value_to_name(dw_tag_t tag) {
  static constexpr llvm::StringLiteral s_unknown_tag_name("<unknown DW_TAG>");
  if (llvm::StringRef tag_name = llvm::dwarf::TagString(tag); !tag_name.empty())
    return tag_name;

  return s_unknown_tag_name;
}

const char *DW_OP_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::OperationEncodingString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_OP constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

} // namespace dwarf
} // namespace lldb_private::plugin
