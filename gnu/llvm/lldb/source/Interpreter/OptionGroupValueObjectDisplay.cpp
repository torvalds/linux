//===-- OptionGroupValueObjectDisplay.cpp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupValueObjectDisplay.h"

#include "lldb/DataFormatters/ValueObjectPrinter.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Target/Target.h"

#include "llvm/ADT/ArrayRef.h"

using namespace lldb;
using namespace lldb_private;

static const OptionDefinition g_option_table[] = {
    {LLDB_OPT_SET_1, false, "dynamic-type", 'd',
     OptionParser::eRequiredArgument, nullptr, GetDynamicValueTypes(), 0,
     eArgTypeNone, "Show the object as its full dynamic type, not its static "
                   "type, if available."},
    {LLDB_OPT_SET_1, false, "synthetic-type", 'S',
     OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean,
     "Show the object obeying its synthetic provider, if available."},
    {LLDB_OPT_SET_1, false, "depth", 'D', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeCount, "Set the max recurse depth when dumping "
                                    "aggregate types (default is infinity)."},
    {LLDB_OPT_SET_1, false, "flat", 'F', OptionParser::eNoArgument, nullptr,
     {}, 0, eArgTypeNone, "Display results in a flat format that uses "
                          "expression paths for each variable or member."},
    {LLDB_OPT_SET_1, false, "location", 'L', OptionParser::eNoArgument, nullptr,
     {}, 0, eArgTypeNone, "Show variable location information."},
    {LLDB_OPT_SET_1, false, "object-description", 'O',
     OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone,
     "Display using a language-specific description API, if possible."},
    {LLDB_OPT_SET_1, false, "ptr-depth", 'P', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeCount, "The number of pointers to be traversed "
                                    "when dumping values (default is zero)."},
    {LLDB_OPT_SET_1, false, "show-types", 'T', OptionParser::eNoArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Show variable types when dumping values."},
    {LLDB_OPT_SET_1, false, "no-summary-depth", 'Y',
     OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeCount,
     "Set the depth at which omitting summary information stops (default is "
     "1)."},
    {LLDB_OPT_SET_1, false, "raw-output", 'R', OptionParser::eNoArgument,
     nullptr, {}, 0, eArgTypeNone, "Don't use formatting options."},
    {LLDB_OPT_SET_1, false, "show-all-children", 'A', OptionParser::eNoArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Ignore the upper bound on the number of children to show."},
    {LLDB_OPT_SET_1, false, "validate", 'V', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeBoolean, "Show results of type validators."},
    {LLDB_OPT_SET_1, false, "element-count", 'Z',
     OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeCount,
     "Treat the result of the expression as if its type is an array of this "
     "many values."}};

llvm::ArrayRef<OptionDefinition>
OptionGroupValueObjectDisplay::GetDefinitions() {
  return llvm::ArrayRef(g_option_table);
}

Status OptionGroupValueObjectDisplay::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;
  const int short_option = g_option_table[option_idx].short_option;
  bool success = false;

  switch (short_option) {
  case 'd': {
    int32_t result;
    result = OptionArgParser::ToOptionEnum(option_arg, GetDynamicValueTypes(),
                                           2, error);
    if (error.Success())
      use_dynamic = (lldb::DynamicValueType)result;
  } break;
  case 'T':
    show_types = true;
    break;
  case 'L':
    show_location = true;
    break;
  case 'F':
    flat_output = true;
    break;
  case 'O':
    use_objc = true;
    break;
  case 'R':
    be_raw = true;
    break;
  case 'A':
    ignore_cap = true;
    break;

  case 'D':
    if (option_arg.getAsInteger(0, max_depth)) {
      max_depth = UINT32_MAX;
      error.SetErrorStringWithFormat("invalid max depth '%s'",
                                     option_arg.str().c_str());
    } else {
      max_depth_is_default = false;
    }
    break;

  case 'Z':
    if (option_arg.getAsInteger(0, elem_count)) {
      elem_count = UINT32_MAX;
      error.SetErrorStringWithFormat("invalid element count '%s'",
                                     option_arg.str().c_str());
    }
    break;

  case 'P':
    if (option_arg.getAsInteger(0, ptr_depth)) {
      ptr_depth = 0;
      error.SetErrorStringWithFormat("invalid pointer depth '%s'",
                                     option_arg.str().c_str());
    }
    break;

  case 'Y':
    if (option_arg.empty())
      no_summary_depth = 1;
    else if (option_arg.getAsInteger(0, no_summary_depth)) {
      no_summary_depth = 0;
      error.SetErrorStringWithFormat("invalid pointer depth '%s'",
                                     option_arg.str().c_str());
    }
    break;

  case 'S':
    use_synth = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (!success)
      error.SetErrorStringWithFormat("invalid synthetic-type '%s'",
                                     option_arg.str().c_str());
    break;

  case 'V':
    run_validator = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (!success)
      error.SetErrorStringWithFormat("invalid validate '%s'",
                                     option_arg.str().c_str());
    break;

  default:
    llvm_unreachable("Unimplemented option");
  }

  return error;
}

void OptionGroupValueObjectDisplay::OptionParsingStarting(
    ExecutionContext *execution_context) {
  // If these defaults change, be sure to modify AnyOptionWasSet().
  show_types = false;
  no_summary_depth = 0;
  show_location = false;
  flat_output = false;
  use_objc = false;
  max_depth = UINT32_MAX;
  max_depth_is_default = true;
  ptr_depth = 0;
  elem_count = 0;
  use_synth = true;
  be_raw = false;
  ignore_cap = false;
  run_validator = false;

  TargetSP target_sp =
      execution_context ? execution_context->GetTargetSP() : TargetSP();
  if (target_sp) {
    use_dynamic = target_sp->GetPreferDynamicValue();
    auto max_depth_config = target_sp->GetMaximumDepthOfChildrenToDisplay();
    max_depth = std::get<uint32_t>(max_depth_config);
    max_depth_is_default = std::get<bool>(max_depth_config);
  } else {
    // If we don't have any targets, then dynamic values won't do us much good.
    use_dynamic = lldb::eNoDynamicValues;
  }
}

DumpValueObjectOptions OptionGroupValueObjectDisplay::GetAsDumpOptions(
    LanguageRuntimeDescriptionDisplayVerbosity lang_descr_verbosity,
    lldb::Format format, lldb::TypeSummaryImplSP summary_sp) {
  DumpValueObjectOptions options;
  options.SetMaximumPointerDepth(
      {DumpValueObjectOptions::PointerDepth::Mode::Always, ptr_depth});
  if (use_objc)
    options.SetShowSummary(false);
  else
    options.SetOmitSummaryDepth(no_summary_depth);
  options.SetMaximumDepth(max_depth, max_depth_is_default)
      .SetShowTypes(show_types)
      .SetShowLocation(show_location)
      .SetUseObjectiveC(use_objc)
      .SetUseDynamicType(use_dynamic)
      .SetUseSyntheticValue(use_synth)
      .SetFlatOutput(flat_output)
      .SetIgnoreCap(ignore_cap)
      .SetFormat(format)
      .SetSummary(summary_sp);

  if (lang_descr_verbosity ==
      eLanguageRuntimeDescriptionDisplayVerbosityCompact)
    options.SetHideRootType(use_objc).SetHideName(use_objc).SetHideValue(
        use_objc);

  if (be_raw)
    options.SetRawDisplay();

  options.SetRunValidator(run_validator);

  options.SetElementCount(elem_count);

  return options;
}
