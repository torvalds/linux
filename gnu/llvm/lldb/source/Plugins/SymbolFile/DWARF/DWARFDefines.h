//===-- DWARFDefines.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEFINES_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEFINES_H

#include "lldb/Core/dwarf.h"
#include <cstdint>

namespace lldb_private::plugin {
namespace dwarf {

llvm::StringRef DW_TAG_value_to_name(dw_tag_t tag);

const char *DW_OP_value_to_name(uint32_t val);

} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEFINES_H
