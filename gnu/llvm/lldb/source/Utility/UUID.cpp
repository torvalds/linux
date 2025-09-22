//===-- UUID.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/UUID.h"

#include "lldb/Utility/Stream.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Format.h"

#include <cctype>
#include <cstdio>
#include <cstring>

using namespace lldb_private;

// Whether to put a separator after count uuid bytes.
// For the first 16 bytes we follow the traditional UUID format. After that, we
// simply put a dash after every 6 bytes.
static inline bool separate(size_t count) {
  if (count >= 10)
    return (count - 10) % 6 == 0;

  switch (count) {
  case 4:
  case 6:
  case 8:
    return true;
  default:
    return false;
  }
}

UUID::UUID(UUID::CvRecordPdb70 debug_info) {
  llvm::sys::swapByteOrder(debug_info.Uuid.Data1);
  llvm::sys::swapByteOrder(debug_info.Uuid.Data2);
  llvm::sys::swapByteOrder(debug_info.Uuid.Data3);
  llvm::sys::swapByteOrder(debug_info.Age);
  if (debug_info.Age)
    *this = UUID(&debug_info, sizeof(debug_info));
  else
    *this = UUID(&debug_info.Uuid, sizeof(debug_info.Uuid));
}

std::string UUID::GetAsString(llvm::StringRef separator) const {
  std::string result;
  llvm::raw_string_ostream os(result);

  for (auto B : llvm::enumerate(GetBytes())) {
    if (separate(B.index()))
      os << separator;

    os << llvm::format_hex_no_prefix(B.value(), 2, true);
  }
  os.flush();

  return result;
}

void UUID::Dump(Stream &s) const { s.PutCString(GetAsString()); }

static inline int xdigit_to_int(char ch) {
  ch = tolower(ch);
  if (ch >= 'a' && ch <= 'f')
    return 10 + ch - 'a';
  return ch - '0';
}

llvm::StringRef
UUID::DecodeUUIDBytesFromString(llvm::StringRef p,
                                llvm::SmallVectorImpl<uint8_t> &uuid_bytes) {
  uuid_bytes.clear();
  while (p.size() >= 2) {
    if (isxdigit(p[0]) && isxdigit(p[1])) {
      int hi_nibble = xdigit_to_int(p[0]);
      int lo_nibble = xdigit_to_int(p[1]);
      // Translate the two hex nibble characters into a byte
      uuid_bytes.push_back((hi_nibble << 4) + lo_nibble);

      // Skip both hex digits
      p = p.drop_front(2);
    } else if (p.front() == '-') {
      // Skip dashes
      p = p.drop_front();
    } else {
      // UUID values can only consist of hex characters and '-' chars
      break;
    }
  }
  return p;
}

bool UUID::SetFromStringRef(llvm::StringRef str) {
  llvm::StringRef p = str;

  // Skip leading whitespace characters
  p = p.ltrim();

  llvm::SmallVector<uint8_t, 20> bytes;
  llvm::StringRef rest = UUID::DecodeUUIDBytesFromString(p, bytes);

  // Return false if we could not consume the entire string or if the parsed
  // UUID is empty.
  if (!rest.empty() || bytes.empty())
    return false;

  *this = UUID(bytes);
  return true;
}
