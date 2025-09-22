//===- Formatters.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/Formatters.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::codeview::detail;

GuidAdapter::GuidAdapter(StringRef Guid)
    : FormatAdapter(ArrayRef(Guid.bytes_begin(), Guid.bytes_end())) {}

GuidAdapter::GuidAdapter(ArrayRef<uint8_t> Guid)
    : FormatAdapter(std::move(Guid)) {}

// From https://docs.microsoft.com/en-us/windows/win32/msi/guid documentation:
// The GUID data type is a text string representing a Class identifier (ID).
// All GUIDs must be authored in uppercase.
// The valid format for a GUID is {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} where
// X is a hex digit (0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F).
//
// The individual string components must be padded to comply with the specific
// lengths of {8-4-4-4-12} characters.
// The llvm-yaml2obj tool checks that a GUID follow that format:
// - the total length to be 38 (including the curly braces.
// - there is a dash at the positions: 8, 13, 18 and 23.
void GuidAdapter::format(raw_ostream &Stream, StringRef Style) {
  assert(Item.size() == 16 && "Expected 16-byte GUID");
  struct MSGuid {
    support::ulittle32_t Data1;
    support::ulittle16_t Data2;
    support::ulittle16_t Data3;
    support::ubig64_t Data4;
  };
  const MSGuid *G = reinterpret_cast<const MSGuid *>(Item.data());
  Stream
      << '{' << format_hex_no_prefix(G->Data1, 8, /*Upper=*/true)
      << '-' << format_hex_no_prefix(G->Data2, 4, /*Upper=*/true)
      << '-' << format_hex_no_prefix(G->Data3, 4, /*Upper=*/true)
      << '-' << format_hex_no_prefix(G->Data4 >> 48, 4, /*Upper=*/true) << '-'
      << format_hex_no_prefix(G->Data4 & ((1ULL << 48) - 1), 12, /*Upper=*/true)
      << '}';
}

raw_ostream &llvm::codeview::operator<<(raw_ostream &OS, const GUID &Guid) {
  codeview::detail::GuidAdapter A(Guid.Guid);
  A.format(OS, "");
  return OS;
}
