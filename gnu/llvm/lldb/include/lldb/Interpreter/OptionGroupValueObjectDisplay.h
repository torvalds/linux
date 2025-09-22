//===-- OptionGroupValueObjectDisplay.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONGROUPVALUEOBJECTDISPLAY_H
#define LLDB_INTERPRETER_OPTIONGROUPVALUEOBJECTDISPLAY_H

#include "lldb/Core/ValueObject.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

// OptionGroupValueObjectDisplay

class OptionGroupValueObjectDisplay : public OptionGroup {
public:
  OptionGroupValueObjectDisplay() = default;

  ~OptionGroupValueObjectDisplay() override = default;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  bool AnyOptionWasSet() const {
    return show_types || no_summary_depth != 0 || show_location ||
           flat_output || use_objc || max_depth != UINT32_MAX ||
           ptr_depth != 0 || !use_synth || be_raw || ignore_cap ||
           run_validator;
  }

  DumpValueObjectOptions GetAsDumpOptions(
      LanguageRuntimeDescriptionDisplayVerbosity lang_descr_verbosity =
          eLanguageRuntimeDescriptionDisplayVerbosityFull,
      lldb::Format format = lldb::eFormatDefault,
      lldb::TypeSummaryImplSP summary_sp = lldb::TypeSummaryImplSP());

  bool show_types : 1, show_location : 1, flat_output : 1, use_objc : 1,
      use_synth : 1, be_raw : 1, ignore_cap : 1, run_validator : 1,
      max_depth_is_default : 1;

  uint32_t no_summary_depth;
  uint32_t max_depth;
  uint32_t ptr_depth;
  uint32_t elem_count;
  lldb::DynamicValueType use_dynamic;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONGROUPVALUEOBJECTDISPLAY_H
