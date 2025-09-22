//===-- flags_parser.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "flags_parser.h"
#include "common.h"
#include "report.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace scudo {

class UnknownFlagsRegistry {
  static const u32 MaxUnknownFlags = 16;
  const char *UnknownFlagsNames[MaxUnknownFlags];
  u32 NumberOfUnknownFlags;

public:
  void add(const char *Name) {
    CHECK_LT(NumberOfUnknownFlags, MaxUnknownFlags);
    UnknownFlagsNames[NumberOfUnknownFlags++] = Name;
  }

  void report() {
    if (!NumberOfUnknownFlags)
      return;
    Printf("Scudo WARNING: found %d unrecognized flag(s):\n",
           NumberOfUnknownFlags);
    for (u32 I = 0; I < NumberOfUnknownFlags; ++I)
      Printf("    %s\n", UnknownFlagsNames[I]);
    NumberOfUnknownFlags = 0;
  }
};
static UnknownFlagsRegistry UnknownFlags;

void reportUnrecognizedFlags() { UnknownFlags.report(); }

void FlagParser::printFlagDescriptions() {
  Printf("Available flags for Scudo:\n");
  for (u32 I = 0; I < NumberOfFlags; ++I)
    Printf("\t%s\n\t\t- %s\n", Flags[I].Name, Flags[I].Desc);
}

static bool isSeparator(char C) {
  return C == ' ' || C == ',' || C == ':' || C == '\n' || C == '\t' ||
         C == '\r';
}

static bool isSeparatorOrNull(char C) { return !C || isSeparator(C); }

void FlagParser::skipWhitespace() {
  while (isSeparator(Buffer[Pos]))
    ++Pos;
}

void FlagParser::parseFlag() {
  const uptr NameStart = Pos;
  while (Buffer[Pos] != '=' && !isSeparatorOrNull(Buffer[Pos]))
    ++Pos;
  if (Buffer[Pos] != '=')
    reportError("expected '='");
  const char *Name = Buffer + NameStart;
  const uptr ValueStart = ++Pos;
  const char *Value;
  if (Buffer[Pos] == '\'' || Buffer[Pos] == '"') {
    const char Quote = Buffer[Pos++];
    while (Buffer[Pos] != 0 && Buffer[Pos] != Quote)
      ++Pos;
    if (Buffer[Pos] == 0)
      reportError("unterminated string");
    Value = Buffer + ValueStart + 1;
    ++Pos; // consume the closing quote
  } else {
    while (!isSeparatorOrNull(Buffer[Pos]))
      ++Pos;
    Value = Buffer + ValueStart;
  }
  if (!runHandler(Name, Value, '='))
    reportError("flag parsing failed.");
}

void FlagParser::parseFlags() {
  while (true) {
    skipWhitespace();
    if (Buffer[Pos] == 0)
      break;
    parseFlag();
  }
}

void FlagParser::parseString(const char *S) {
  if (!S)
    return;
  // Backup current parser state to allow nested parseString() calls.
  const char *OldBuffer = Buffer;
  const uptr OldPos = Pos;
  Buffer = S;
  Pos = 0;

  parseFlags();

  Buffer = OldBuffer;
  Pos = OldPos;
}

inline bool parseBool(const char *Value, bool *b) {
  if (strncmp(Value, "0", 1) == 0 || strncmp(Value, "no", 2) == 0 ||
      strncmp(Value, "false", 5) == 0) {
    *b = false;
    return true;
  }
  if (strncmp(Value, "1", 1) == 0 || strncmp(Value, "yes", 3) == 0 ||
      strncmp(Value, "true", 4) == 0) {
    *b = true;
    return true;
  }
  return false;
}

void FlagParser::parseStringPair(const char *Name, const char *Value) {
  if (!runHandler(Name, Value, '\0'))
    reportError("flag parsing failed.");
}

bool FlagParser::runHandler(const char *Name, const char *Value,
                            const char Sep) {
  for (u32 I = 0; I < NumberOfFlags; ++I) {
    const uptr Len = strlen(Flags[I].Name);
    if (strncmp(Name, Flags[I].Name, Len) != 0 || Name[Len] != Sep)
      continue;
    bool Ok = false;
    switch (Flags[I].Type) {
    case FlagType::FT_bool:
      Ok = parseBool(Value, reinterpret_cast<bool *>(Flags[I].Var));
      if (!Ok)
        reportInvalidFlag("bool", Value);
      break;
    case FlagType::FT_int:
      char *ValueEnd;
      errno = 0;
      long V = strtol(Value, &ValueEnd, 10);
      if (errno != 0 ||                 // strtol failed (over or underflow)
          V > INT_MAX || V < INT_MIN || // overflows integer
          // contains unexpected characters
          (*ValueEnd != '"' && *ValueEnd != '\'' &&
           !isSeparatorOrNull(*ValueEnd))) {
        reportInvalidFlag("int", Value);
        break;
      }
      *reinterpret_cast<int *>(Flags[I].Var) = static_cast<int>(V);
      Ok = true;
      break;
    }
    return Ok;
  }
  // Unrecognized flag. This is not a fatal error, we may print a warning later.
  UnknownFlags.add(Name);
  return true;
}

void FlagParser::registerFlag(const char *Name, const char *Desc, FlagType Type,
                              void *Var) {
  CHECK_LT(NumberOfFlags, MaxFlags);
  Flags[NumberOfFlags].Name = Name;
  Flags[NumberOfFlags].Desc = Desc;
  Flags[NumberOfFlags].Type = Type;
  Flags[NumberOfFlags].Var = Var;
  ++NumberOfFlags;
}

} // namespace scudo
