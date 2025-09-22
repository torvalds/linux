//===-- DIERef.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DIERef.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/DataExtractor.h"
#include "llvm/Support/Format.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

void llvm::format_provider<DIERef>::format(const DIERef &ref, raw_ostream &OS,
                                           StringRef Style) {
  if (ref.file_index())
    OS << format_hex_no_prefix(*ref.file_index(), 8) << "/";
  OS << (ref.section() == DIERef::DebugInfo ? "INFO" : "TYPE");
  OS << "/" << format_hex_no_prefix(ref.die_offset(), 8);
}

std::optional<DIERef> DIERef::Decode(const DataExtractor &data,
                                     lldb::offset_t *offset_ptr) {
  DIERef die_ref(data.GetU64(offset_ptr));

  // DIE offsets can't be zero and if we fail to decode something from data,
  // it will return 0
  if (!die_ref.die_offset())
    return std::nullopt;

  return die_ref;
}

void DIERef::Encode(DataEncoder &encoder) const { encoder.AppendU64(get_id()); }
