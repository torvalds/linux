//===-- options_parser.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/optional/options_parser.h"
#include "gwp_asan/optional/printf.h"
#include "gwp_asan/utilities.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace {
enum class OptionType : uint8_t {
  OT_bool,
  OT_int,
};

#define InvokeIfNonNull(Printf, ...)                                           \
  do {                                                                         \
    if (Printf)                                                                \
      Printf(__VA_ARGS__);                                                     \
  } while (0);

class OptionParser {
public:
  explicit OptionParser(gwp_asan::Printf_t PrintfForWarnings)
      : Printf(PrintfForWarnings) {}
  void registerOption(const char *Name, const char *Desc, OptionType Type,
                      void *Var);
  void parseString(const char *S);
  void printOptionDescriptions();

private:
  // Calculate at compile-time how many options are available.
#define GWP_ASAN_OPTION(...) +1
  static constexpr size_t MaxOptions = 0
#include "gwp_asan/options.inc"
      ;
#undef GWP_ASAN_OPTION

  struct Option {
    const char *Name;
    const char *Desc;
    OptionType Type;
    void *Var;
  } Options[MaxOptions];

  size_t NumberOfOptions = 0;
  const char *Buffer = nullptr;
  uintptr_t Pos = 0;
  gwp_asan::Printf_t Printf = nullptr;

  void skipWhitespace();
  void parseOptions();
  bool parseOption();
  bool setOptionToValue(const char *Name, const char *Value);
};

void OptionParser::printOptionDescriptions() {
  InvokeIfNonNull(Printf, "GWP-ASan: Available options:\n");
  for (size_t I = 0; I < NumberOfOptions; ++I)
    InvokeIfNonNull(Printf, "\t%s\n\t\t- %s\n", Options[I].Name,
                    Options[I].Desc);
}

bool isSeparator(char C) {
  return C == ' ' || C == ',' || C == ':' || C == '\n' || C == '\t' ||
         C == '\r';
}

bool isSeparatorOrNull(char C) { return !C || isSeparator(C); }

void OptionParser::skipWhitespace() {
  while (isSeparator(Buffer[Pos]))
    ++Pos;
}

bool OptionParser::parseOption() {
  const uintptr_t NameStart = Pos;
  while (Buffer[Pos] != '=' && !isSeparatorOrNull(Buffer[Pos]))
    ++Pos;

  const char *Name = Buffer + NameStart;
  if (Buffer[Pos] != '=') {
    InvokeIfNonNull(Printf, "GWP-ASan: Expected '=' when parsing option '%s'.",
                    Name);
    return false;
  }
  const uintptr_t ValueStart = ++Pos;
  const char *Value;
  if (Buffer[Pos] == '\'' || Buffer[Pos] == '"') {
    const char Quote = Buffer[Pos++];
    while (Buffer[Pos] != 0 && Buffer[Pos] != Quote)
      ++Pos;
    if (Buffer[Pos] == 0) {
      InvokeIfNonNull(Printf, "GWP-ASan: Unterminated string in option '%s'.",
                      Name);
      return false;
    }
    Value = Buffer + ValueStart + 1;
    ++Pos; // consume the closing quote
  } else {
    while (!isSeparatorOrNull(Buffer[Pos]))
      ++Pos;
    Value = Buffer + ValueStart;
  }

  return setOptionToValue(Name, Value);
}

void OptionParser::parseOptions() {
  while (true) {
    skipWhitespace();
    if (Buffer[Pos] == 0)
      break;
    if (!parseOption()) {
      InvokeIfNonNull(Printf, "GWP-ASan: Options parsing failed.\n");
      return;
    }
  }
}

void OptionParser::parseString(const char *S) {
  if (!S)
    return;
  Buffer = S;
  Pos = 0;
  parseOptions();
}

bool parseBool(const char *Value, bool *b) {
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

bool OptionParser::setOptionToValue(const char *Name, const char *Value) {
  for (size_t I = 0; I < NumberOfOptions; ++I) {
    const uintptr_t Len = strlen(Options[I].Name);
    if (strncmp(Name, Options[I].Name, Len) != 0 || Name[Len] != '=')
      continue;
    bool Ok = false;
    switch (Options[I].Type) {
    case OptionType::OT_bool:
      Ok = parseBool(Value, reinterpret_cast<bool *>(Options[I].Var));
      if (!Ok)
        InvokeIfNonNull(
            Printf, "GWP-ASan: Invalid boolean value '%s' for option '%s'.\n",
            Value, Options[I].Name);
      break;
    case OptionType::OT_int:
      char *ValueEnd;
      *reinterpret_cast<int *>(Options[I].Var) =
          static_cast<int>(strtol(Value, &ValueEnd, 10));
      Ok =
          *ValueEnd == '"' || *ValueEnd == '\'' || isSeparatorOrNull(*ValueEnd);
      if (!Ok)
        InvokeIfNonNull(
            Printf, "GWP-ASan: Invalid integer value '%s' for option '%s'.\n",
            Value, Options[I].Name);
      break;
    }
    return Ok;
  }

  InvokeIfNonNull(Printf, "GWP-ASan: Unknown option '%s'.", Name);
  return true;
}

void OptionParser::registerOption(const char *Name, const char *Desc,
                                  OptionType Type, void *Var) {
  assert(NumberOfOptions < MaxOptions &&
         "GWP-ASan Error: Ran out of space for options.\n");
  Options[NumberOfOptions].Name = Name;
  Options[NumberOfOptions].Desc = Desc;
  Options[NumberOfOptions].Type = Type;
  Options[NumberOfOptions].Var = Var;
  ++NumberOfOptions;
}

void registerGwpAsanOptions(OptionParser *parser,
                            gwp_asan::options::Options *o) {
#define GWP_ASAN_OPTION(Type, Name, DefaultValue, Description)                 \
  parser->registerOption(#Name, Description, OptionType::OT_##Type, &o->Name);
#include "gwp_asan/options.inc"
#undef GWP_ASAN_OPTION
}

const char *getGwpAsanDefaultOptions() {
  return (__gwp_asan_default_options) ? __gwp_asan_default_options() : "";
}

gwp_asan::options::Options *getOptionsInternal() {
  static gwp_asan::options::Options GwpAsanOptions;
  return &GwpAsanOptions;
}
} // anonymous namespace

namespace gwp_asan {
namespace options {

void initOptions(const char *OptionsStr, Printf_t PrintfForWarnings) {
  Options *o = getOptionsInternal();
  o->setDefaults();

  OptionParser Parser(PrintfForWarnings);
  registerGwpAsanOptions(&Parser, o);

  // Override from the weak function definition in this executable.
  Parser.parseString(getGwpAsanDefaultOptions());

  // Override from the provided options string.
  Parser.parseString(OptionsStr);

  if (o->help)
    Parser.printOptionDescriptions();

  if (!o->Enabled)
    return;

  if (o->MaxSimultaneousAllocations <= 0) {
    InvokeIfNonNull(
        PrintfForWarnings,
        "GWP-ASan ERROR: MaxSimultaneousAllocations must be > 0 when GWP-ASan "
        "is enabled.\n");
    o->Enabled = false;
  }
  if (o->SampleRate <= 0) {
    InvokeIfNonNull(
        PrintfForWarnings,
        "GWP-ASan ERROR: SampleRate must be > 0 when GWP-ASan is enabled.\n");
    o->Enabled = false;
  }
}

void initOptions(Printf_t PrintfForWarnings) {
  initOptions(getenv("GWP_ASAN_OPTIONS"), PrintfForWarnings);
}

Options &getOptions() { return *getOptionsInternal(); }

} // namespace options
} // namespace gwp_asan
