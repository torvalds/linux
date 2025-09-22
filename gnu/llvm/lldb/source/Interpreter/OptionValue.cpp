//===-- OptionValue.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Interpreter/OptionValues.h"
#include "lldb/Utility/StringList.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

OptionValue::OptionValue(const OptionValue &other) {
  std::lock_guard<std::mutex> lock(other.m_mutex);

  m_parent_wp = other.m_parent_wp;
  m_callback = other.m_callback;
  m_value_was_set = other.m_value_was_set;

}

OptionValue& OptionValue::operator=(const OptionValue &other) {
  std::scoped_lock<std::mutex, std::mutex> lock(m_mutex, other.m_mutex);

  m_parent_wp = other.m_parent_wp;
  m_callback = other.m_callback;
  m_value_was_set = other.m_value_was_set;

  return *this;
}

Status OptionValue::SetSubValue(const ExecutionContext *exe_ctx,
                                VarSetOperationType op, llvm::StringRef name,
                                llvm::StringRef value) {
  Status error;
  error.SetErrorString("SetSubValue is not supported");
  return error;
}

OptionValueBoolean *OptionValue::GetAsBoolean() {
  if (GetType() == OptionValue::eTypeBoolean)
    return static_cast<OptionValueBoolean *>(this);
  return nullptr;
}

const OptionValueBoolean *OptionValue::GetAsBoolean() const {
  if (GetType() == OptionValue::eTypeBoolean)
    return static_cast<const OptionValueBoolean *>(this);
  return nullptr;
}

const OptionValueChar *OptionValue::GetAsChar() const {
  if (GetType() == OptionValue::eTypeChar)
    return static_cast<const OptionValueChar *>(this);
  return nullptr;
}

OptionValueChar *OptionValue::GetAsChar() {
  if (GetType() == OptionValue::eTypeChar)
    return static_cast<OptionValueChar *>(this);
  return nullptr;
}

OptionValueFileSpec *OptionValue::GetAsFileSpec() {
  if (GetType() == OptionValue::eTypeFileSpec)
    return static_cast<OptionValueFileSpec *>(this);
  return nullptr;
}

const OptionValueFileSpec *OptionValue::GetAsFileSpec() const {
  if (GetType() == OptionValue::eTypeFileSpec)
    return static_cast<const OptionValueFileSpec *>(this);
  return nullptr;
}

OptionValueFileSpecList *OptionValue::GetAsFileSpecList() {
  if (GetType() == OptionValue::eTypeFileSpecList)
    return static_cast<OptionValueFileSpecList *>(this);
  return nullptr;
}

const OptionValueFileSpecList *OptionValue::GetAsFileSpecList() const {
  if (GetType() == OptionValue::eTypeFileSpecList)
    return static_cast<const OptionValueFileSpecList *>(this);
  return nullptr;
}

OptionValueArch *OptionValue::GetAsArch() {
  if (GetType() == OptionValue::eTypeArch)
    return static_cast<OptionValueArch *>(this);
  return nullptr;
}

const OptionValueArch *OptionValue::GetAsArch() const {
  if (GetType() == OptionValue::eTypeArch)
    return static_cast<const OptionValueArch *>(this);
  return nullptr;
}

OptionValueArray *OptionValue::GetAsArray() {
  if (GetType() == OptionValue::eTypeArray)
    return static_cast<OptionValueArray *>(this);
  return nullptr;
}

const OptionValueArray *OptionValue::GetAsArray() const {
  if (GetType() == OptionValue::eTypeArray)
    return static_cast<const OptionValueArray *>(this);
  return nullptr;
}

OptionValueArgs *OptionValue::GetAsArgs() {
  if (GetType() == OptionValue::eTypeArgs)
    return static_cast<OptionValueArgs *>(this);
  return nullptr;
}

const OptionValueArgs *OptionValue::GetAsArgs() const {
  if (GetType() == OptionValue::eTypeArgs)
    return static_cast<const OptionValueArgs *>(this);
  return nullptr;
}

OptionValueDictionary *OptionValue::GetAsDictionary() {
  if (GetType() == OptionValue::eTypeDictionary)
    return static_cast<OptionValueDictionary *>(this);
  return nullptr;
}

const OptionValueDictionary *OptionValue::GetAsDictionary() const {
  if (GetType() == OptionValue::eTypeDictionary)
    return static_cast<const OptionValueDictionary *>(this);
  return nullptr;
}

OptionValueEnumeration *OptionValue::GetAsEnumeration() {
  if (GetType() == OptionValue::eTypeEnum)
    return static_cast<OptionValueEnumeration *>(this);
  return nullptr;
}

const OptionValueEnumeration *OptionValue::GetAsEnumeration() const {
  if (GetType() == OptionValue::eTypeEnum)
    return static_cast<const OptionValueEnumeration *>(this);
  return nullptr;
}

OptionValueFormat *OptionValue::GetAsFormat() {
  if (GetType() == OptionValue::eTypeFormat)
    return static_cast<OptionValueFormat *>(this);
  return nullptr;
}

const OptionValueFormat *OptionValue::GetAsFormat() const {
  if (GetType() == OptionValue::eTypeFormat)
    return static_cast<const OptionValueFormat *>(this);
  return nullptr;
}

OptionValueLanguage *OptionValue::GetAsLanguage() {
  if (GetType() == OptionValue::eTypeLanguage)
    return static_cast<OptionValueLanguage *>(this);
  return nullptr;
}

const OptionValueLanguage *OptionValue::GetAsLanguage() const {
  if (GetType() == OptionValue::eTypeLanguage)
    return static_cast<const OptionValueLanguage *>(this);
  return nullptr;
}

OptionValueFormatEntity *OptionValue::GetAsFormatEntity() {
  if (GetType() == OptionValue::eTypeFormatEntity)
    return static_cast<OptionValueFormatEntity *>(this);
  return nullptr;
}

const OptionValueFormatEntity *OptionValue::GetAsFormatEntity() const {
  if (GetType() == OptionValue::eTypeFormatEntity)
    return static_cast<const OptionValueFormatEntity *>(this);
  return nullptr;
}

OptionValuePathMappings *OptionValue::GetAsPathMappings() {
  if (GetType() == OptionValue::eTypePathMap)
    return static_cast<OptionValuePathMappings *>(this);
  return nullptr;
}

const OptionValuePathMappings *OptionValue::GetAsPathMappings() const {
  if (GetType() == OptionValue::eTypePathMap)
    return static_cast<const OptionValuePathMappings *>(this);
  return nullptr;
}

OptionValueProperties *OptionValue::GetAsProperties() {
  if (GetType() == OptionValue::eTypeProperties)
    return static_cast<OptionValueProperties *>(this);
  return nullptr;
}

const OptionValueProperties *OptionValue::GetAsProperties() const {
  if (GetType() == OptionValue::eTypeProperties)
    return static_cast<const OptionValueProperties *>(this);
  return nullptr;
}

OptionValueRegex *OptionValue::GetAsRegex() {
  if (GetType() == OptionValue::eTypeRegex)
    return static_cast<OptionValueRegex *>(this);
  return nullptr;
}

const OptionValueRegex *OptionValue::GetAsRegex() const {
  if (GetType() == OptionValue::eTypeRegex)
    return static_cast<const OptionValueRegex *>(this);
  return nullptr;
}

OptionValueSInt64 *OptionValue::GetAsSInt64() {
  if (GetType() == OptionValue::eTypeSInt64)
    return static_cast<OptionValueSInt64 *>(this);
  return nullptr;
}

const OptionValueSInt64 *OptionValue::GetAsSInt64() const {
  if (GetType() == OptionValue::eTypeSInt64)
    return static_cast<const OptionValueSInt64 *>(this);
  return nullptr;
}

OptionValueString *OptionValue::GetAsString() {
  if (GetType() == OptionValue::eTypeString)
    return static_cast<OptionValueString *>(this);
  return nullptr;
}

const OptionValueString *OptionValue::GetAsString() const {
  if (GetType() == OptionValue::eTypeString)
    return static_cast<const OptionValueString *>(this);
  return nullptr;
}

OptionValueUInt64 *OptionValue::GetAsUInt64() {
  if (GetType() == OptionValue::eTypeUInt64)
    return static_cast<OptionValueUInt64 *>(this);
  return nullptr;
}

const OptionValueUInt64 *OptionValue::GetAsUInt64() const {
  if (GetType() == OptionValue::eTypeUInt64)
    return static_cast<const OptionValueUInt64 *>(this);
  return nullptr;
}

OptionValueUUID *OptionValue::GetAsUUID() {
  if (GetType() == OptionValue::eTypeUUID)
    return static_cast<OptionValueUUID *>(this);
  return nullptr;
}

const OptionValueUUID *OptionValue::GetAsUUID() const {
  if (GetType() == OptionValue::eTypeUUID)
    return static_cast<const OptionValueUUID *>(this);
  return nullptr;
}

std::optional<bool> OptionValue::GetBooleanValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueBoolean *option_value = GetAsBoolean())
    return option_value->GetCurrentValue();
  return {};
}

bool OptionValue::SetBooleanValue(bool new_value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueBoolean *option_value = GetAsBoolean()) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

std::optional<char> OptionValue::GetCharValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueChar *option_value = GetAsChar())
    return option_value->GetCurrentValue();
  return {};
}

bool OptionValue::SetCharValue(char new_value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueChar *option_value = GetAsChar()) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

std::optional<int64_t> OptionValue::GetEnumerationValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueEnumeration *option_value = GetAsEnumeration())
    return option_value->GetCurrentValue();
  return {};
}

bool OptionValue::SetEnumerationValue(int64_t value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueEnumeration *option_value = GetAsEnumeration()) {
    option_value->SetCurrentValue(value);
    return true;
  }
  return false;
}

std::optional<FileSpec> OptionValue::GetFileSpecValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueFileSpec *option_value = GetAsFileSpec())
    return option_value->GetCurrentValue();
  return {};
}

bool OptionValue::SetFileSpecValue(FileSpec file_spec) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueFileSpec *option_value = GetAsFileSpec()) {
    option_value->SetCurrentValue(file_spec, false);
    return true;
  }
  return false;
}

bool OptionValue::AppendFileSpecValue(FileSpec file_spec) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueFileSpecList *option_value = GetAsFileSpecList()) {
    option_value->AppendCurrentValue(file_spec);
    return true;
  }
  return false;
}

std::optional<FileSpecList> OptionValue::GetFileSpecListValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueFileSpecList *option_value = GetAsFileSpecList())
    return option_value->GetCurrentValue();
  return {};
}

std::optional<lldb::Format> OptionValue::GetFormatValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueFormat *option_value = GetAsFormat())
    return option_value->GetCurrentValue();
  return {};
}

bool OptionValue::SetFormatValue(lldb::Format new_value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueFormat *option_value = GetAsFormat()) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

std::optional<lldb::LanguageType> OptionValue::GetLanguageValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueLanguage *option_value = GetAsLanguage())
    return option_value->GetCurrentValue();
  return {};
}

bool OptionValue::SetLanguageValue(lldb::LanguageType new_language) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueLanguage *option_value = GetAsLanguage()) {
    option_value->SetCurrentValue(new_language);
    return true;
  }
  return false;
}

const FormatEntity::Entry *OptionValue::GetFormatEntity() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueFormatEntity *option_value = GetAsFormatEntity())
    return &option_value->GetCurrentValue();
  return nullptr;
}

const RegularExpression *OptionValue::GetRegexValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueRegex *option_value = GetAsRegex())
    return option_value->GetCurrentValue();
  return nullptr;
}

std::optional<int64_t> OptionValue::GetSInt64Value() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueSInt64 *option_value = GetAsSInt64())
    return option_value->GetCurrentValue();
  return {};
}

bool OptionValue::SetSInt64Value(int64_t new_value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueSInt64 *option_value = GetAsSInt64()) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

std::optional<llvm::StringRef> OptionValue::GetStringValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueString *option_value = GetAsString())
    return option_value->GetCurrentValueAsRef();
  return {};
}

bool OptionValue::SetStringValue(llvm::StringRef new_value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueString *option_value = GetAsString()) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

std::optional<uint64_t> OptionValue::GetUInt64Value() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueUInt64 *option_value = GetAsUInt64())
    return option_value->GetCurrentValue();
  return {};
}

bool OptionValue::SetUInt64Value(uint64_t new_value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueUInt64 *option_value = GetAsUInt64()) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

std::optional<UUID> OptionValue::GetUUIDValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueUUID *option_value = GetAsUUID())
    return option_value->GetCurrentValue();
  return {};
}

bool OptionValue::SetUUIDValue(const UUID &uuid) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueUUID *option_value = GetAsUUID()) {
    option_value->SetCurrentValue(uuid);
    return true;
  }
  return false;
}

std::optional<ArchSpec> OptionValue::GetArchSpecValue() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const OptionValueArch *option_value = GetAsArch())
    return option_value->GetCurrentValue();
  return {};
}

bool OptionValue::SetArchSpecValue(ArchSpec arch_spec) {
    std::lock_guard<std::mutex> lock(m_mutex);
  if (OptionValueArch *option_value = GetAsArch()) {
    option_value->SetCurrentValue(arch_spec, false);
    return true;
  }
  return false;
}

const char *OptionValue::GetBuiltinTypeAsCString(Type t) {
  switch (t) {
  case eTypeInvalid:
    return "invalid";
  case eTypeArch:
    return "arch";
  case eTypeArgs:
    return "arguments";
  case eTypeArray:
    return "array";
  case eTypeBoolean:
    return "boolean";
  case eTypeChar:
    return "char";
  case eTypeDictionary:
    return "dictionary";
  case eTypeEnum:
    return "enum";
  case eTypeFileLineColumn:
    return "file:line:column specifier";
  case eTypeFileSpec:
    return "file";
  case eTypeFileSpecList:
    return "file-list";
  case eTypeFormat:
    return "format";
  case eTypeFormatEntity:
    return "format-string";
  case eTypeLanguage:
    return "language";
  case eTypePathMap:
    return "path-map";
  case eTypeProperties:
    return "properties";
  case eTypeRegex:
    return "regex";
  case eTypeSInt64:
    return "int";
  case eTypeString:
    return "string";
  case eTypeUInt64:
    return "unsigned";
  case eTypeUUID:
    return "uuid";
  }
  return nullptr;
}

lldb::OptionValueSP OptionValue::CreateValueFromCStringForTypeMask(
    const char *value_cstr, uint32_t type_mask, Status &error) {
  // If only 1 bit is set in the type mask for a dictionary or array then we
  // know how to decode a value from a cstring
  lldb::OptionValueSP value_sp;
  switch (type_mask) {
  case 1u << eTypeArch:
    value_sp = std::make_shared<OptionValueArch>();
    break;
  case 1u << eTypeBoolean:
    value_sp = std::make_shared<OptionValueBoolean>(false);
    break;
  case 1u << eTypeChar:
    value_sp = std::make_shared<OptionValueChar>('\0');
    break;
  case 1u << eTypeFileSpec:
    value_sp = std::make_shared<OptionValueFileSpec>();
    break;
  case 1u << eTypeFormat:
    value_sp = std::make_shared<OptionValueFormat>(eFormatInvalid);
    break;
  case 1u << eTypeFormatEntity:
    value_sp = std::make_shared<OptionValueFormatEntity>(nullptr);
    break;
  case 1u << eTypeLanguage:
    value_sp = std::make_shared<OptionValueLanguage>(eLanguageTypeUnknown);
    break;
  case 1u << eTypeSInt64:
    value_sp = std::make_shared<OptionValueSInt64>();
    break;
  case 1u << eTypeString:
    value_sp = std::make_shared<OptionValueString>();
    break;
  case 1u << eTypeUInt64:
    value_sp = std::make_shared<OptionValueUInt64>();
    break;
  case 1u << eTypeUUID:
    value_sp = std::make_shared<OptionValueUUID>();
    break;
  }

  if (value_sp)
    error = value_sp->SetValueFromString(value_cstr, eVarSetOperationAssign);
  else
    error.SetErrorString("unsupported type mask");
  return value_sp;
}

bool OptionValue::DumpQualifiedName(Stream &strm) const {
  bool dumped_something = false;
  lldb::OptionValueSP m_parent_sp(m_parent_wp.lock());
  if (m_parent_sp) {
    if (m_parent_sp->DumpQualifiedName(strm))
      dumped_something = true;
  }
  llvm::StringRef name(GetName());
  if (!name.empty()) {
    if (dumped_something)
      strm.PutChar('.');
    else
      dumped_something = true;
    strm << name;
  }
  return dumped_something;
}

OptionValueSP OptionValue::DeepCopy(const OptionValueSP &new_parent) const {
  auto clone = Clone();
  clone->SetParent(new_parent);
  return clone;
}

void OptionValue::AutoComplete(CommandInterpreter &interpreter,
                               CompletionRequest &request) {}

Status OptionValue::SetValueFromString(llvm::StringRef value,
                                       VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationReplace:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'replace' operation",
        GetTypeAsCString());
    break;
  case eVarSetOperationInsertBefore:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'insert-before' operation",
        GetTypeAsCString());
    break;
  case eVarSetOperationInsertAfter:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'insert-after' operation",
        GetTypeAsCString());
    break;
  case eVarSetOperationRemove:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'remove' operation", GetTypeAsCString());
    break;
  case eVarSetOperationAppend:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'append' operation", GetTypeAsCString());
    break;
  case eVarSetOperationClear:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'clear' operation", GetTypeAsCString());
    break;
  case eVarSetOperationAssign:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'assign' operation", GetTypeAsCString());
    break;
  case eVarSetOperationInvalid:
    error.SetErrorStringWithFormat("invalid operation performed on a %s object",
                                   GetTypeAsCString());
    break;
  }
  return error;
}
