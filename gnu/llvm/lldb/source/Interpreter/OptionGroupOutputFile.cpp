//===-- OptionGroupOutputFile.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupOutputFile.h"

#include "lldb/Host/OptionParser.h"

using namespace lldb;
using namespace lldb_private;

OptionGroupOutputFile::OptionGroupOutputFile() : m_append(false, false) {}

static const uint32_t SHORT_OPTION_APND = 0x61706e64; // 'apnd'

static constexpr OptionDefinition g_option_table[] = {
    {LLDB_OPT_SET_1, false, "outfile", 'o', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeFilename,
     "Specify a path for capturing command output."},
    {LLDB_OPT_SET_1, false, "append-outfile", SHORT_OPTION_APND,
     OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone,
     "Append to the file specified with '--outfile <path>'."},
};

llvm::ArrayRef<OptionDefinition> OptionGroupOutputFile::GetDefinitions() {
  return llvm::ArrayRef(g_option_table);
}

Status
OptionGroupOutputFile::SetOptionValue(uint32_t option_idx,
                                      llvm::StringRef option_arg,
                                      ExecutionContext *execution_context) {
  Status error;
  const int short_option = g_option_table[option_idx].short_option;

  switch (short_option) {
  case 'o':
    error = m_file.SetValueFromString(option_arg);
    break;

  case SHORT_OPTION_APND:
    m_append.SetCurrentValue(true);
    break;

  default:
    llvm_unreachable("Unimplemented option");
  }

  return error;
}

void OptionGroupOutputFile::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_file.Clear();
  m_append.Clear();
}
