//===-- OptionGroupPythonClassWithDict.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupPythonClassWithDict.h"

#include "lldb/Host/OptionParser.h"

using namespace lldb;
using namespace lldb_private;

OptionGroupPythonClassWithDict::OptionGroupPythonClassWithDict(
    const char *class_use, bool is_class, int class_option, int key_option,
    int value_option, uint16_t required_options)
    : m_is_class(is_class), m_required_options(required_options) {
  m_key_usage_text.assign("The key for a key/value pair passed to the "
                          "implementation of a ");
  m_key_usage_text.append(class_use);
  m_key_usage_text.append(".  Pairs can be specified more than once.");
  
  m_value_usage_text.assign("The value for the previous key in the pair passed "
                            "to the implementation of a ");
  m_value_usage_text.append(class_use);
  m_value_usage_text.append(".  Pairs can be specified more than once.");
  
  m_class_usage_text.assign("The name of the ");
  m_class_usage_text.append(m_is_class ? "class" : "function");
  m_class_usage_text.append(" that will manage a ");
  m_class_usage_text.append(class_use);
  m_class_usage_text.append(".");
  
  m_option_definition[0].usage_mask = LLDB_OPT_SET_1;
  m_option_definition[0].required = m_required_options.Test(eScriptClass);
  m_option_definition[0].long_option = "script-class";
  m_option_definition[0].short_option = class_option;
  m_option_definition[0].validator = nullptr;
  m_option_definition[0].option_has_arg = OptionParser::eRequiredArgument;
  m_option_definition[0].enum_values = {};
  m_option_definition[0].completion_type = 0;
  m_option_definition[0].argument_type = eArgTypePythonClass;
  m_option_definition[0].usage_text = m_class_usage_text.data();

  m_option_definition[1].usage_mask = LLDB_OPT_SET_2;
  m_option_definition[1].required = m_required_options.Test(eDictKey);
  m_option_definition[1].long_option = "structured-data-key";
  m_option_definition[1].short_option = key_option;
  m_option_definition[1].validator = nullptr;
  m_option_definition[1].option_has_arg = OptionParser::eRequiredArgument;
  m_option_definition[1].enum_values = {};
  m_option_definition[1].completion_type = 0;
  m_option_definition[1].argument_type = eArgTypeNone;
  m_option_definition[1].usage_text = m_key_usage_text.data();

  m_option_definition[2].usage_mask = LLDB_OPT_SET_2;
  m_option_definition[2].required = m_required_options.Test(eDictValue);
  m_option_definition[2].long_option = "structured-data-value";
  m_option_definition[2].short_option = value_option;
  m_option_definition[2].validator = nullptr;
  m_option_definition[2].option_has_arg = OptionParser::eRequiredArgument;
  m_option_definition[2].enum_values = {};
  m_option_definition[2].completion_type = 0;
  m_option_definition[2].argument_type = eArgTypeNone;
  m_option_definition[2].usage_text = m_value_usage_text.data();
  
  m_option_definition[3].usage_mask = LLDB_OPT_SET_3;
  m_option_definition[3].required = m_required_options.Test(ePythonFunction);
  m_option_definition[3].long_option = "python-function";
  m_option_definition[3].short_option = class_option;
  m_option_definition[3].validator = nullptr;
  m_option_definition[3].option_has_arg = OptionParser::eRequiredArgument;
  m_option_definition[3].enum_values = {};
  m_option_definition[3].completion_type = 0;
  m_option_definition[3].argument_type = eArgTypePythonFunction;
  m_option_definition[3].usage_text = m_class_usage_text.data();
}

Status OptionGroupPythonClassWithDict::SetOptionValue(
    uint32_t option_idx,
    llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;
  switch (option_idx) {
  case 0:
  case 3: {
    m_name.assign(std::string(option_arg));
  } break;
  case 1: {
      if (!m_dict_sp)
        m_dict_sp = std::make_shared<StructuredData::Dictionary>();
      if (m_current_key.empty())
        m_current_key.assign(std::string(option_arg));
      else
        error.SetErrorStringWithFormat("Key: \"%s\" missing value.",
                                        m_current_key.c_str());
    
  } break;
  case 2: {
      if (!m_dict_sp)
        m_dict_sp = std::make_shared<StructuredData::Dictionary>();
      if (!m_current_key.empty()) {
        if (!option_arg.empty()) {
          double d = 0;
          std::string opt = option_arg.lower();

          if (llvm::to_integer(option_arg, d)) {
            if (opt[0] == '-')
              m_dict_sp->AddIntegerItem(m_current_key, static_cast<int64_t>(d));
            else
              m_dict_sp->AddIntegerItem(m_current_key,
                                        static_cast<uint64_t>(d));
          } else if (llvm::to_float(option_arg, d)) {
            m_dict_sp->AddFloatItem(m_current_key, d);
          } else if (opt == "true" || opt == "false") {
            m_dict_sp->AddBooleanItem(m_current_key, opt == "true");
          } else {
            m_dict_sp->AddStringItem(m_current_key, option_arg);
          }
        }

        m_current_key.clear();
      }
      else
        error.SetErrorStringWithFormat("Value: \"%s\" missing matching key.",
                                       option_arg.str().c_str());
  } break;
  default:
    llvm_unreachable("Unimplemented option");
  }
  return error;
}

void OptionGroupPythonClassWithDict::OptionParsingStarting(
  ExecutionContext *execution_context) {
  m_current_key.erase();
  // Leave the dictionary shared pointer unset.  That way you can tell that
  // the user didn't pass any -k -v pairs.  We want to be able to warn if these
  // were passed when the function they passed won't use them.
  m_dict_sp.reset();
  m_name.clear();
}

Status OptionGroupPythonClassWithDict::OptionParsingFinished(
  ExecutionContext *execution_context) {
  Status error;
  // If we get here and there's contents in the m_current_key, somebody must
  // have provided a key but no value.
  if (!m_current_key.empty())
      error.SetErrorStringWithFormat("Key: \"%s\" missing value.",
                                     m_current_key.c_str());
  return error;
}

