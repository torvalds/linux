//===-- OptionGroupMemoryTag.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupMemoryTag.h"

#include "lldb/Host/OptionParser.h"

using namespace lldb;
using namespace lldb_private;

static const uint32_t SHORT_OPTION_SHOW_TAGS = 0x54414753; // 'tags'

OptionGroupMemoryTag::OptionGroupMemoryTag(bool note_binary /*=false*/)
    : m_show_tags(false, false), m_option_definition{
                                     LLDB_OPT_SET_1,
                                     false,
                                     "show-tags",
                                     SHORT_OPTION_SHOW_TAGS,
                                     OptionParser::eNoArgument,
                                     nullptr,
                                     {},
                                     0,
                                     eArgTypeNone,
                                     note_binary
                                         ? "Include memory tags in output "
                                           "(does not apply to binary output)."
                                         : "Include memory tags in output."} {}

llvm::ArrayRef<OptionDefinition> OptionGroupMemoryTag::GetDefinitions() {
  return llvm::ArrayRef(m_option_definition);
}

Status
OptionGroupMemoryTag::SetOptionValue(uint32_t option_idx,
                                     llvm::StringRef option_arg,
                                     ExecutionContext *execution_context) {
  assert(option_idx == 0 && "Only one option in memory tag group!");

  switch (m_option_definition.short_option) {
  case SHORT_OPTION_SHOW_TAGS:
    m_show_tags.SetCurrentValue(true);
    m_show_tags.SetOptionWasSet();
    break;

  default:
    llvm_unreachable("Unimplemented option");
  }

  return {};
}

void OptionGroupMemoryTag::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_show_tags.Clear();
}
