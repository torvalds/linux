//===-- OptionGroupVariable.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONGROUPVARIABLE_H
#define LLDB_INTERPRETER_OPTIONGROUPVARIABLE_H

#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

// OptionGroupVariable

class OptionGroupVariable : public OptionGroup {
public:
  OptionGroupVariable(bool show_frame_options);

  ~OptionGroupVariable() override = default;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  bool include_frame_options : 1,
      show_args : 1,    // Frame option only (include_frame_options == true)
      show_recognized_args : 1,  // Frame option only (include_frame_options ==
                                 // true)
      show_locals : 1,  // Frame option only (include_frame_options == true)
      show_globals : 1, // Frame option only (include_frame_options == true)
      use_regex : 1, show_scope : 1, show_decl : 1;
  OptionValueString summary;        // the name of a named summary
  OptionValueString summary_string; // a summary string

private:
  OptionGroupVariable(const OptionGroupVariable &) = delete;
  const OptionGroupVariable &operator=(const OptionGroupVariable &) = delete;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONGROUPVARIABLE_H
