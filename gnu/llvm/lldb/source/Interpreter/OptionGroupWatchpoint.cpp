//===-- OptionGroupWatchpoint.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupWatchpoint.h"

#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Target/Language.h"
#include "lldb/lldb-enumerations.h"

using namespace lldb;
using namespace lldb_private;

static constexpr OptionEnumValueElement g_watch_type[] = {
    {
        OptionGroupWatchpoint::eWatchRead,
        "read",
        "Watch for read",
    },
    {
        OptionGroupWatchpoint::eWatchWrite,
        "write",
        "Watch for write",
    },
    {
        OptionGroupWatchpoint::eWatchModify,
        "modify",
        "Watch for modifications",
    },
    {
        OptionGroupWatchpoint::eWatchReadWrite,
        "read_write",
        "Watch for read/write",
    },
};

static constexpr OptionDefinition g_option_table[] = {
    {LLDB_OPT_SET_1, false, "watch", 'w', OptionParser::eRequiredArgument,
     nullptr, OptionEnumValues(g_watch_type), 0, eArgTypeWatchType,
     "Specify the type of watching to perform."},
    {LLDB_OPT_SET_1, false, "size", 's', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeByteSize, 
     "Number of bytes to use to watch a region."},
    {LLDB_OPT_SET_2,
     false,
     "language",
     'l',
     OptionParser::eRequiredArgument,
     nullptr,
     {},
     0,
     eArgTypeLanguage,
     "Language of expression to run"}};

Status
OptionGroupWatchpoint::SetOptionValue(uint32_t option_idx,
                                      llvm::StringRef option_arg,
                                      ExecutionContext *execution_context) {
  Status error;
  const int short_option = g_option_table[option_idx].short_option;
  switch (short_option) {
  case 'l': {
    language_type = Language::GetLanguageTypeFromString(option_arg);
    if (language_type == eLanguageTypeUnknown) {
      StreamString sstr;
      sstr.Printf("Unknown language type: '%s' for expression. List of "
                  "supported languages:\n",
                  option_arg.str().c_str());
      Language::PrintSupportedLanguagesForExpressions(sstr, " ", "\n");
      error.SetErrorString(sstr.GetString());
    }
    break;
  }
  case 'w': {
    WatchType tmp_watch_type;
    tmp_watch_type = (WatchType)OptionArgParser::ToOptionEnum(
        option_arg, g_option_table[option_idx].enum_values, 0, error);
    if (error.Success()) {
      watch_type = tmp_watch_type;
      watch_type_specified = true;
    }
    break;
  }
  case 's':
    error = watch_size.SetValueFromString(option_arg);
    if (watch_size.GetCurrentValue() == 0)
      error.SetErrorStringWithFormat("invalid --size option value '%s'",
                                     option_arg.str().c_str());
    break;

  default:
    llvm_unreachable("Unimplemented option");
  }

  return error;
}

void OptionGroupWatchpoint::OptionParsingStarting(
    ExecutionContext *execution_context) {
  watch_type_specified = false;
  watch_type = eWatchInvalid;
  watch_size.Clear();
  language_type = eLanguageTypeUnknown;
}

llvm::ArrayRef<OptionDefinition> OptionGroupWatchpoint::GetDefinitions() {
  return llvm::ArrayRef(g_option_table);
}
