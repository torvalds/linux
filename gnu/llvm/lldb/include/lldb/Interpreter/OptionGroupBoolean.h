//===-- OptionGroupBoolean.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONGROUPBOOLEAN_H
#define LLDB_INTERPRETER_OPTIONGROUPBOOLEAN_H

#include "lldb/Interpreter/OptionValueBoolean.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {
// OptionGroupBoolean

class OptionGroupBoolean : public OptionGroup {
public:
  // When 'no_argument_toggle_default' is true, then setting the option value
  // does NOT require an argument, it sets the boolean value to the inverse of
  // the default value
  OptionGroupBoolean(uint32_t usage_mask, bool required,
                     const char *long_option, int short_option,
                     const char *usage_text, bool default_value,
                     bool no_argument_toggle_default);

  ~OptionGroupBoolean() override = default;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
    return llvm::ArrayRef<OptionDefinition>(&m_option_definition, 1);
  }

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  OptionValueBoolean &GetOptionValue() { return m_value; }

  const OptionValueBoolean &GetOptionValue() const { return m_value; }

protected:
  OptionValueBoolean m_value;
  OptionDefinition m_option_definition;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONGROUPBOOLEAN_H
