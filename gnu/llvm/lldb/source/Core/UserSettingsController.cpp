//===-- UserSettingsController.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/UserSettingsController.h"

#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

#include <memory>

namespace lldb_private {
class CommandInterpreter;
}
namespace lldb_private {
class ConstString;
}
namespace lldb_private {
class ExecutionContext;
}
namespace lldb_private {
class Property;
}

using namespace lldb;
using namespace lldb_private;

Properties::Properties() = default;

Properties::Properties(const lldb::OptionValuePropertiesSP &collection_sp)
    : m_collection_sp(collection_sp) {}

Properties::~Properties() = default;

lldb::OptionValueSP
Properties::GetPropertyValue(const ExecutionContext *exe_ctx,
                             llvm::StringRef path, Status &error) const {
  OptionValuePropertiesSP properties_sp(GetValueProperties());
  if (properties_sp)
    return properties_sp->GetSubValue(exe_ctx, path, error);
  return lldb::OptionValueSP();
}

Status Properties::SetPropertyValue(const ExecutionContext *exe_ctx,
                                    VarSetOperationType op,
                                    llvm::StringRef path,
                                    llvm::StringRef value) {
  OptionValuePropertiesSP properties_sp(GetValueProperties());
  if (properties_sp)
    return properties_sp->SetSubValue(exe_ctx, op, path, value);
  Status error;
  error.SetErrorString("no properties");
  return error;
}

void Properties::DumpAllPropertyValues(const ExecutionContext *exe_ctx,
                                       Stream &strm, uint32_t dump_mask,
                                       bool is_json) {
  OptionValuePropertiesSP properties_sp(GetValueProperties());
  if (!properties_sp)
    return;

  if (is_json) {
    llvm::json::Value json = properties_sp->ToJSON(exe_ctx);
    strm.Printf("%s", llvm::formatv("{0:2}", json).str().c_str());
  } else
    properties_sp->DumpValue(exe_ctx, strm, dump_mask);
}

void Properties::DumpAllDescriptions(CommandInterpreter &interpreter,
                                     Stream &strm) const {
  strm.PutCString("Top level variables:\n\n");

  OptionValuePropertiesSP properties_sp(GetValueProperties());
  if (properties_sp)
    return properties_sp->DumpAllDescriptions(interpreter, strm);
}

Status Properties::DumpPropertyValue(const ExecutionContext *exe_ctx,
                                     Stream &strm,
                                     llvm::StringRef property_path,
                                     uint32_t dump_mask, bool is_json) {
  OptionValuePropertiesSP properties_sp(GetValueProperties());
  if (properties_sp) {
    return properties_sp->DumpPropertyValue(exe_ctx, strm, property_path,
                                            dump_mask, is_json);
  }
  Status error;
  error.SetErrorString("empty property list");
  return error;
}

size_t
Properties::Apropos(llvm::StringRef keyword,
                    std::vector<const Property *> &matching_properties) const {
  OptionValuePropertiesSP properties_sp(GetValueProperties());
  if (properties_sp) {
    properties_sp->Apropos(keyword, matching_properties);
  }
  return matching_properties.size();
}

llvm::StringRef Properties::GetExperimentalSettingsName() {
  static constexpr llvm::StringLiteral g_experimental("experimental");
  return g_experimental;
}

bool Properties::IsSettingExperimental(llvm::StringRef setting) {
  if (setting.empty())
    return false;

  llvm::StringRef experimental = GetExperimentalSettingsName();
  size_t dot_pos = setting.find_first_of('.');
  return setting.take_front(dot_pos) == experimental;
}
