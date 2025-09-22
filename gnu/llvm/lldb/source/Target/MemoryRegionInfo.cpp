//===-- MemoryRegionInfo.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/MemoryRegionInfo.h"

using namespace lldb_private;

llvm::raw_ostream &lldb_private::operator<<(llvm::raw_ostream &OS,
                                            const MemoryRegionInfo &Info) {
  return OS << llvm::formatv("MemoryRegionInfo([{0}, {1}), {2:r}{3:w}{4:x}, "
                             "{5}, `{6}`, {7}, {8}, {9})",
                             Info.GetRange().GetRangeBase(),
                             Info.GetRange().GetRangeEnd(), Info.GetReadable(),
                             Info.GetWritable(), Info.GetExecutable(),
                             Info.GetMapped(), Info.GetName(), Info.GetFlash(),
                             Info.GetBlocksize(), Info.GetMemoryTagged());
}

void llvm::format_provider<MemoryRegionInfo::OptionalBool>::format(
    const MemoryRegionInfo::OptionalBool &B, raw_ostream &OS,
    StringRef Options) {
  assert(Options.size() <= 1);
  bool Empty = Options.empty();
  switch (B) {
  case lldb_private::MemoryRegionInfo::eNo:
    OS << (Empty ? "no" : "-");
    return;
  case lldb_private::MemoryRegionInfo::eYes:
    OS << (Empty ? "yes" : Options);
    return;
  case lldb_private::MemoryRegionInfo::eDontKnow:
    OS << (Empty ? "don't know" : "?");
    return;
  }
}
