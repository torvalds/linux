//===-- flags_parser.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_FLAGS_PARSER_H_
#define SCUDO_FLAGS_PARSER_H_

#include "report.h"
#include "string_utils.h"

#include <stddef.h>

namespace scudo {

enum class FlagType : u8 {
  FT_bool,
  FT_int,
};

class FlagParser {
public:
  void registerFlag(const char *Name, const char *Desc, FlagType Type,
                    void *Var);
  void parseString(const char *S);
  void printFlagDescriptions();
  void parseStringPair(const char *Name, const char *Value);

private:
  static const u32 MaxFlags = 20;
  struct Flag {
    const char *Name;
    const char *Desc;
    FlagType Type;
    void *Var;
  } Flags[MaxFlags];

  u32 NumberOfFlags = 0;
  const char *Buffer = nullptr;
  uptr Pos = 0;

  void reportFatalError(const char *Error);
  void skipWhitespace();
  void parseFlags();
  void parseFlag();
  bool runHandler(const char *Name, const char *Value, char Sep);
};

void reportUnrecognizedFlags();

} // namespace scudo

#endif // SCUDO_FLAGS_PARSER_H_
