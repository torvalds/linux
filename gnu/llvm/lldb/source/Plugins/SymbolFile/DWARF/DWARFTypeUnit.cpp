//===-- DWARFTypeUnit.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFTypeUnit.h"

#include "SymbolFileDWARF.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

void DWARFTypeUnit::Dump(Stream *s) const {
  s->Format("{0:x16}: Type Unit: length = {1:x8}, version = {2:x4}, "
            "abbr_offset = {3:x8}, addr_size = {4:x2} (next CU at "
            "[{5:x16}])\n",
            GetOffset(), (uint32_t)GetLength(), GetVersion(),
            (uint32_t)GetAbbrevOffset(), GetAddressByteSize(),
            GetNextUnitOffset());
}
