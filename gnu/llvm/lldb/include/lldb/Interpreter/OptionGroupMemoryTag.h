//===-- OptionGroupMemoryTag.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONGROUPMEMORYTAG_H
#define LLDB_INTERPRETER_OPTIONGROUPMEMORYTAG_H

#include "lldb/Interpreter/OptionValueBoolean.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

class OptionGroupMemoryTag : public OptionGroup {
public:
  OptionGroupMemoryTag(
      // Whether to note that --show-tags does not apply to binary output.
      // "memory read" wants this but "memory find" does not.
      bool note_binary = false);

  ~OptionGroupMemoryTag() override = default;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  bool AnyOptionWasSet() const { return m_show_tags.OptionWasSet(); }

  OptionValueBoolean GetShowTags() { return m_show_tags; };

protected:
  OptionValueBoolean m_show_tags;
  OptionDefinition m_option_definition;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONGROUPMEMORYTAG_H
