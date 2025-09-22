//===- DWARFTypeUnit.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFTypeUnit.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cinttypes>

using namespace llvm;

void DWARFTypeUnit::dump(raw_ostream &OS, DIDumpOptions DumpOpts) {
  DWARFDie TD = getDIEForOffset(getTypeOffset() + getOffset());
  const char *Name = TD.getName(DINameKind::ShortName);
  int OffsetDumpWidth = 2 * dwarf::getDwarfOffsetByteSize(getFormat());

  if (DumpOpts.SummarizeTypes) {
    OS << "name = '" << Name << "'"
       << ", type_signature = " << format("0x%016" PRIx64, getTypeHash())
       << ", length = " << format("0x%0*" PRIx64, OffsetDumpWidth, getLength())
       << '\n';
    return;
  }

  OS << format("0x%08" PRIx64, getOffset()) << ": Type Unit:"
     << " length = " << format("0x%0*" PRIx64, OffsetDumpWidth, getLength())
     << ", format = " << dwarf::FormatString(getFormat())
     << ", version = " << format("0x%04x", getVersion());
  if (getVersion() >= 5)
    OS << ", unit_type = " << dwarf::UnitTypeString(getUnitType());
  OS << ", abbr_offset = " << format("0x%04" PRIx64, getAbbrOffset());
  if (!getAbbreviations())
    OS << " (invalid)";
  OS << ", addr_size = " << format("0x%02x", getAddressByteSize())
     << ", name = '" << Name << "'"
     << ", type_signature = " << format("0x%016" PRIx64, getTypeHash())
     << ", type_offset = " << format("0x%04" PRIx64, getTypeOffset())
     << " (next unit at " << format("0x%08" PRIx64, getNextUnitOffset())
     << ")\n";

  if (DWARFDie TU = getUnitDIE(false))
    TU.dump(OS, 0, DumpOpts);
  else
    OS << "<type unit can't be parsed!>\n\n";
}
