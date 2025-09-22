//===-- OptionValue.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONVALUE_H
#define LLDB_INTERPRETER_OPTIONVALUE_H

#include "lldb/Core/FormatEntity.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Cloneable.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/UUID.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-private-interfaces.h"
#include "llvm/Support/JSON.h"
#include <mutex>

namespace lldb_private {

// OptionValue
class OptionValue {
public:
  enum Type {
    eTypeInvalid = 0,
    eTypeArch,
    eTypeArgs,
    eTypeArray,
    eTypeBoolean,
    eTypeChar,
    eTypeDictionary,
    eTypeEnum,
    eTypeFileLineColumn,
    eTypeFileSpec,
    eTypeFileSpecList,
    eTypeFormat,
    eTypeLanguage,
    eTypePathMap,
    eTypeProperties,
    eTypeRegex,
    eTypeSInt64,
    eTypeString,
    eTypeUInt64,
    eTypeUUID,
    eTypeFormatEntity
  };

  enum {
    eDumpOptionName = (1u << 0),
    eDumpOptionType = (1u << 1),
    eDumpOptionValue = (1u << 2),
    eDumpOptionDescription = (1u << 3),
    eDumpOptionRaw = (1u << 4),
    eDumpOptionCommand = (1u << 5),
    eDumpGroupValue = (eDumpOptionName | eDumpOptionType | eDumpOptionValue),
    eDumpGroupHelp =
        (eDumpOptionName | eDumpOptionType | eDumpOptionDescription),
    eDumpGroupExport = (eDumpOptionCommand | eDumpOptionName | eDumpOptionValue)
  };

  OptionValue() = default;

  virtual ~OptionValue() = default;

  OptionValue(const OptionValue &other);
  
  OptionValue& operator=(const OptionValue &other);

  // Subclasses should override these functions
  virtual Type GetType() const = 0;

  // If this value is always hidden, the avoid showing any info on this value,
  // just show the info for the child values.
  virtual bool ValueIsTransparent() const {
    return GetType() == eTypeProperties;
  }

  virtual const char *GetTypeAsCString() const {
    return GetBuiltinTypeAsCString(GetType());
  }

  static const char *GetBuiltinTypeAsCString(Type t);

  virtual void DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                         uint32_t dump_mask) = 0;

  // TODO: make this function pure virtual after implementing it in all
  // child classes.
  virtual llvm::json::Value ToJSON(const ExecutionContext *exe_ctx) {
    // Return nullptr which will create a llvm::json::Value() that is a NULL
    // value. No setting should ever really have a NULL value in JSON. This
    // indicates an error occurred and if/when we add a FromJSON() it will know
    // to fail if someone tries to set it with a NULL JSON value.
    return nullptr;
  }

  virtual Status
  SetValueFromString(llvm::StringRef value,
                     VarSetOperationType op = eVarSetOperationAssign);

  virtual void Clear() = 0;

  virtual lldb::OptionValueSP
  DeepCopy(const lldb::OptionValueSP &new_parent) const;

  virtual void AutoComplete(CommandInterpreter &interpreter,
                            CompletionRequest &request);

  // Subclasses can override these functions
  virtual lldb::OptionValueSP GetSubValue(const ExecutionContext *exe_ctx,
                                          llvm::StringRef name,
                                          Status &error) const {
    error.SetErrorStringWithFormatv("'{0}' is not a valid subvalue", name);
    return lldb::OptionValueSP();
  }

  virtual Status SetSubValue(const ExecutionContext *exe_ctx,
                             VarSetOperationType op, llvm::StringRef name,
                             llvm::StringRef value);

  virtual bool IsAggregateValue() const { return false; }

  virtual llvm::StringRef GetName() const { return llvm::StringRef(); }

  virtual bool DumpQualifiedName(Stream &strm) const;

  // Subclasses should NOT override these functions as they use the above
  // functions to implement functionality
  uint32_t GetTypeAsMask() { return 1u << GetType(); }

  static uint32_t ConvertTypeToMask(OptionValue::Type type) {
    return 1u << type;
  }

  static OptionValue::Type ConvertTypeMaskToType(uint32_t type_mask) {
    // If only one bit is set, then return an appropriate enumeration
    switch (type_mask) {
    case 1u << eTypeArch:
      return eTypeArch;
    case 1u << eTypeArgs:
      return eTypeArgs;
    case 1u << eTypeArray:
      return eTypeArray;
    case 1u << eTypeBoolean:
      return eTypeBoolean;
    case 1u << eTypeChar:
      return eTypeChar;
    case 1u << eTypeDictionary:
      return eTypeDictionary;
    case 1u << eTypeEnum:
      return eTypeEnum;
    case 1u << eTypeFileLineColumn:
      return eTypeFileLineColumn;
    case 1u << eTypeFileSpec:
      return eTypeFileSpec;
    case 1u << eTypeFileSpecList:
      return eTypeFileSpecList;
    case 1u << eTypeFormat:
      return eTypeFormat;
    case 1u << eTypeLanguage:
      return eTypeLanguage;
    case 1u << eTypePathMap:
      return eTypePathMap;
    case 1u << eTypeProperties:
      return eTypeProperties;
    case 1u << eTypeRegex:
      return eTypeRegex;
    case 1u << eTypeSInt64:
      return eTypeSInt64;
    case 1u << eTypeString:
      return eTypeString;
    case 1u << eTypeUInt64:
      return eTypeUInt64;
    case 1u << eTypeUUID:
      return eTypeUUID;
    }
    // Else return invalid
    return eTypeInvalid;
  }

  static lldb::OptionValueSP
  CreateValueFromCStringForTypeMask(const char *value_cstr, uint32_t type_mask,
                                    Status &error);

  OptionValueArch *GetAsArch();
  const OptionValueArch *GetAsArch() const;

  OptionValueArray *GetAsArray();
  const OptionValueArray *GetAsArray() const;

  OptionValueArgs *GetAsArgs();
  const OptionValueArgs *GetAsArgs() const;

  OptionValueBoolean *GetAsBoolean();
  const OptionValueBoolean *GetAsBoolean() const;

  OptionValueChar *GetAsChar();
  const OptionValueChar *GetAsChar() const;

  OptionValueDictionary *GetAsDictionary();
  const OptionValueDictionary *GetAsDictionary() const;

  OptionValueEnumeration *GetAsEnumeration();
  const OptionValueEnumeration *GetAsEnumeration() const;

  OptionValueFileSpec *GetAsFileSpec();
  const OptionValueFileSpec *GetAsFileSpec() const;

  OptionValueFileSpecList *GetAsFileSpecList();
  const OptionValueFileSpecList *GetAsFileSpecList() const;

  OptionValueFormat *GetAsFormat();
  const OptionValueFormat *GetAsFormat() const;

  OptionValueLanguage *GetAsLanguage();
  const OptionValueLanguage *GetAsLanguage() const;

  OptionValuePathMappings *GetAsPathMappings();
  const OptionValuePathMappings *GetAsPathMappings() const;

  OptionValueProperties *GetAsProperties();
  const OptionValueProperties *GetAsProperties() const;

  OptionValueRegex *GetAsRegex();
  const OptionValueRegex *GetAsRegex() const;

  OptionValueSInt64 *GetAsSInt64();
  const OptionValueSInt64 *GetAsSInt64() const;

  OptionValueString *GetAsString();
  const OptionValueString *GetAsString() const;

  OptionValueUInt64 *GetAsUInt64();
  const OptionValueUInt64 *GetAsUInt64() const;

  OptionValueUUID *GetAsUUID();
  const OptionValueUUID *GetAsUUID() const;

  OptionValueFormatEntity *GetAsFormatEntity();
  const OptionValueFormatEntity *GetAsFormatEntity() const;

  bool AppendFileSpecValue(FileSpec file_spec);

  bool OptionWasSet() const { return m_value_was_set; }

  void SetOptionWasSet() { m_value_was_set = true; }

  void SetParent(const lldb::OptionValueSP &parent_sp) {
    m_parent_wp = parent_sp;
  }

  lldb::OptionValueSP GetParent() const { return m_parent_wp.lock(); }

  void SetValueChangedCallback(std::function<void()> callback) {
    m_callback = std::move(callback);
  }

  void NotifyValueChanged() {
    if (m_callback)
      m_callback();
  }

  template <typename T, std::enable_if_t<!std::is_pointer_v<T>, bool> = true>
  std::optional<T> GetValueAs() const {
    if constexpr (std::is_same_v<T, uint64_t>)
      return GetUInt64Value();
    if constexpr (std::is_same_v<T, int64_t>)
      return GetSInt64Value();
    if constexpr (std::is_same_v<T, bool>)
      return GetBooleanValue();
    if constexpr (std::is_same_v<T, char>)
      return GetCharValue();
    if constexpr (std::is_same_v<T, lldb::Format>)
      return GetFormatValue();
    if constexpr (std::is_same_v<T, FileSpec>)
      return GetFileSpecValue();
    if constexpr (std::is_same_v<T, FileSpecList>)
      return GetFileSpecListValue();
    if constexpr (std::is_same_v<T, lldb::LanguageType>)
      return GetLanguageValue();
    if constexpr (std::is_same_v<T, llvm::StringRef>)
      return GetStringValue();
    if constexpr (std::is_same_v<T, ArchSpec>)
      return GetArchSpecValue();
    if constexpr (std::is_enum_v<T>)
      if (std::optional<int64_t> value = GetEnumerationValue())
        return static_cast<T>(*value);
    return {};
  }

  template <typename T,
            typename U = typename std::remove_const<
                typename std::remove_pointer<T>::type>::type,
            std::enable_if_t<std::is_pointer_v<T>, bool> = true>
  T GetValueAs() const {
    if constexpr (std::is_same_v<U, FormatEntity::Entry>)
      return GetFormatEntity();
    if constexpr (std::is_same_v<U, RegularExpression>)
      return GetRegexValue();
    return {};
  }

  bool SetValueAs(bool v) { return SetBooleanValue(v); }

  bool SetValueAs(char v) { return SetCharValue(v); }

  bool SetValueAs(uint64_t v) { return SetUInt64Value(v); }

  bool SetValueAs(int64_t v) { return SetSInt64Value(v); }

  bool SetValueAs(UUID v) { return SetUUIDValue(v); }

  bool SetValueAs(llvm::StringRef v) { return SetStringValue(v); }

  bool SetValueAs(lldb::LanguageType v) { return SetLanguageValue(v); }

  bool SetValueAs(lldb::Format v) { return SetFormatValue(v); }

  bool SetValueAs(FileSpec v) { return SetFileSpecValue(v); }

  bool SetValueAs(ArchSpec v) { return SetArchSpecValue(v); }

  template <typename T, std::enable_if_t<std::is_enum_v<T>, bool> = true>
  bool SetValueAs(T t) {
    return SetEnumerationValue(t);
  }

protected:
  using TopmostBase = OptionValue;

  // Must be overriden by a derived class for correct downcasting the result of
  // DeepCopy to it. Inherit from Cloneable to avoid doing this manually.
  virtual lldb::OptionValueSP Clone() const = 0;

  lldb::OptionValueWP m_parent_wp;
  std::function<void()> m_callback;
  bool m_value_was_set = false; // This can be used to see if a value has been
                                // set by a call to SetValueFromCString(). It is
                                // often handy to know if an option value was
                                // set from the command line or as a setting,
                                // versus if we just have the default value that
                                // was already populated in the option value.
private:
  std::optional<ArchSpec> GetArchSpecValue() const;
  bool SetArchSpecValue(ArchSpec arch_spec);

  std::optional<bool> GetBooleanValue() const;
  bool SetBooleanValue(bool new_value);

  std::optional<char> GetCharValue() const;
  bool SetCharValue(char new_value);

  std::optional<int64_t> GetEnumerationValue() const;
  bool SetEnumerationValue(int64_t value);

  std::optional<FileSpec> GetFileSpecValue() const;
  bool SetFileSpecValue(FileSpec file_spec);

  std::optional<FileSpecList> GetFileSpecListValue() const;

  std::optional<int64_t> GetSInt64Value() const;
  bool SetSInt64Value(int64_t new_value);

  std::optional<uint64_t> GetUInt64Value() const;
  bool SetUInt64Value(uint64_t new_value);

  std::optional<lldb::Format> GetFormatValue() const;
  bool SetFormatValue(lldb::Format new_value);

  std::optional<lldb::LanguageType> GetLanguageValue() const;
  bool SetLanguageValue(lldb::LanguageType new_language);

  std::optional<llvm::StringRef> GetStringValue() const;
  bool SetStringValue(llvm::StringRef new_value);

  std::optional<UUID> GetUUIDValue() const;
  bool SetUUIDValue(const UUID &uuid);

  const FormatEntity::Entry *GetFormatEntity() const;
  const RegularExpression *GetRegexValue() const;
  
  mutable std::mutex m_mutex;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONVALUE_H
