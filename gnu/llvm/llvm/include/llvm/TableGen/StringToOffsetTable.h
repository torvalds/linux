//===- StringToOffsetTable.h - Emit a big concatenated string ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_STRINGTOOFFSETTABLE_H
#define LLVM_TABLEGEN_STRINGTOOFFSETTABLE_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>

namespace llvm {

/// StringToOffsetTable - This class uniques a bunch of nul-terminated strings
/// and keeps track of their offset in a massive contiguous string allocation.
/// It can then output this string blob and use indexes into the string to
/// reference each piece.
class StringToOffsetTable {
  StringMap<unsigned> StringOffset;
  std::string AggregateString;

public:
  bool Empty() const { return StringOffset.empty(); }

  unsigned GetOrAddStringOffset(StringRef Str, bool appendZero = true) {
    auto IterBool =
        StringOffset.insert(std::make_pair(Str, AggregateString.size()));
    if (IterBool.second) {
      // Add the string to the aggregate if this is the first time found.
      AggregateString.append(Str.begin(), Str.end());
      if (appendZero)
        AggregateString += '\0';
    }

    return IterBool.first->second;
  }

  void EmitString(raw_ostream &O) {
    // Escape the string.
    SmallString<256> Str;
    raw_svector_ostream(Str).write_escaped(AggregateString);
    AggregateString = std::string(Str);

    O << "    \"";
    unsigned CharsPrinted = 0;
    for (unsigned i = 0, e = AggregateString.size(); i != e; ++i) {
      if (CharsPrinted > 70) {
        O << "\"\n    \"";
        CharsPrinted = 0;
      }
      O << AggregateString[i];
      ++CharsPrinted;

      // Print escape sequences all together.
      if (AggregateString[i] != '\\')
        continue;

      assert(i + 1 < AggregateString.size() && "Incomplete escape sequence!");
      if (isdigit(AggregateString[i + 1])) {
        assert(isdigit(AggregateString[i + 2]) &&
               isdigit(AggregateString[i + 3]) &&
               "Expected 3 digit octal escape!");
        O << AggregateString[++i];
        O << AggregateString[++i];
        O << AggregateString[++i];
        CharsPrinted += 3;
      } else {
        O << AggregateString[++i];
        ++CharsPrinted;
      }
    }
    O << "\"";
  }

  /// Emit the string using character literals. MSVC has a limitation that
  /// string literals cannot be longer than 64K.
  void EmitCharArray(raw_ostream &O) {
    assert(AggregateString.find(')') == std::string::npos &&
           "can't emit raw string with closing parens");
    int Count = 0;
    O << ' ';
    for (char C : AggregateString) {
      O << " \'";
      O.write_escaped(StringRef(&C, 1));
      O << "\',";
      Count++;
      if (Count > 14) {
        O << "\n ";
        Count = 0;
      }
    }
    O << '\n';
  }
};

} // end namespace llvm

#endif
